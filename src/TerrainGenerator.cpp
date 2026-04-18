#include "TerrainGenerator.h"

#include <algorithm>
#include <cmath>

// --- Biome table ---------------------------------------------------------
// Surface + subsurface blocks per biome. underwaterSurface lets generateChunkData
// paint the block type that shows at the shoreline (SAND for warm biomes,
// GRAVEL for cold). hasSnow forces SNOW on top at high altitude / cold temp.
const BiomeParams TerrainGenerator::BIOME_TABLE[BIOME_COUNT] = {
    // surface       subsurface   underwater   treeDens  treeChance  snow
    {GRAVEL, STONE, GRAVEL, 0.0f, 0.0f, false},  // DEEP_OCEAN
    {GRAVEL, STONE, GRAVEL, 0.0f, 0.0f, false},  // OCEAN
    {GRASS, DIRT, SAND, 0.0f, 0.0f, false},      // RIVER (seeded through PV + low cont)
    {SAND, SAND, SAND, 0.0f, 0.0f, false},       // BEACH
    {SAND, SAND, SAND, 0.0f, 0.0f, true},        // SNOWY_BEACH
    {STONE, STONE, GRAVEL, 0.0f, 0.0f, false},   // STONE_SHORE
    {GRASS, DIRT, SAND, 0.15f, 20.0f, false},    // PLAINS
    {GRASS, DIRT, SAND, 0.8f, 50.0f, false},     // FOREST
    {GRASS, DIRT, GRAVEL, 0.5f, 40.0f, true},    // TAIGA (cool + trees)
    {GRASS, DIRT, SAND, 0.25f, 35.0f, false},    // SWAMP
    {SAND, SAND, SAND, 0.0f, 0.0f, false},       // DESERT
    {SNOW, DIRT, GRAVEL, 0.0f, 0.0f, true},      // TUNDRA
    {GRASS, DIRT, SAND, 0.05f, 15.0f, false},    // MEADOW (high + flatter)
    {STONE, DIRT, GRAVEL, 0.0f, 0.0f, false},    // WINDSWEPT_HILLS
    {STONE, STONE, GRAVEL, 0.0f, 0.0f, false},   // STONY_PEAKS
    {SNOW, STONE, GRAVEL, 0.0f, 0.0f, true},     // SNOWY_PEAKS
};

namespace {
constexpr double CLIMATE_FREQ_CONT = 0.004;
constexpr double CLIMATE_FREQ_EROSION = 0.006;
constexpr double CLIMATE_FREQ_WEIRD = 0.012;
constexpr double CLIMATE_FREQ_TEMP = 0.009;
constexpr double CLIMATE_FREQ_HUMID = 0.011;
constexpr double DENSITY_FREQ_XZ = 0.02;
constexpr double DENSITY_FREQ_Y = 0.03;
constexpr double CAVE_FREQ = 0.04;
constexpr double ORE_FREQ = 0.08;
}  // namespace

// Independent Perlin instances — derived seeds avoid cross-correlation from
// feeding one noise at offset coords, which was the old approach.
TerrainGenerator::TerrainGenerator(unsigned int seed, float scale, int /*minHeight*/, int maxHeight)
    : perlinNoise(seed), noiseTemp(seed ^ 0xA1),
      noiseHumid(seed ^ 0x7E2D), noiseCont(seed ^ 0xC041),
      noiseErosion(seed ^ 0x8F0B), noiseWeird(seed ^ 0x5D3F),
      noise3DA(seed ^ 0x2B11), noise3DB(seed ^ 0x9E77),
      noiseCaveCheese(seed ^ 0x4C0D), noiseCaveSpaghetti(seed ^ 0x12A7),
      scale(scale), maxHeight(maxHeight) {}

double TerrainGenerator::octaveNoise(double x, double y, int octaves, double persistence, double lacunarity) {
    double total = 0.0, amplitude = 1.0, frequency = 1.0, maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        total += perlinNoise.noise(x * frequency, y * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxVal;
}

double TerrainGenerator::octaveNoise3(double x, double y, double z, int octaves, double persistence, double lacunarity) {
    double total = 0.0, amplitude = 1.0, frequency = 1.0, maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        total += noise3DA.noise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxVal;
}

// Generic fractal-octave sum driven by an explicit PerlinNoise instance.
// Lets each climate parameter keep its own permutation table so they don't
// cross-correlate — a single shared-noise-with-offsets would.
double TerrainGenerator::octaveNoiseFrom(PerlinNoise& p, double x, double y, int octaves, double persistence,
                                         double lacunarity) {
    double total = 0.0, amplitude = 1.0, frequency = 1.0, maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        total += p.noise(x * frequency, y * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxVal;
}

// Ridged fractal: 1 - |n|, then octave-sum. Produces sharp peaks where the
// noise crosses zero, mimicking Minecraft's "jagged peaks" look.
double TerrainGenerator::ridgedNoiseFrom(PerlinNoise& p, double x, double y, int octaves, double persistence,
                                         double lacunarity) {
    double total = 0.0, amplitude = 1.0, frequency = 1.0, maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        double n = 1.0 - std::abs(p.noise(x * frequency, y * frequency));
        total += n * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return (total / maxVal) * 2.0 - 1.0;  // remap [0, 1] → [-1, 1]
}

// --- Climate sampling ---------------------------------------------------

Climate TerrainGenerator::sampleClimate(int x, int z) {
    double nx = x * scale;
    double nz = z * scale;
    Climate c;
    // Fractal sums are tight bell curves (stddev ≈ 0.15 at 5 octaves). Gains
    // stretch the distribution so biome thresholds at ±0.5 actually fire; a
    // gain of ~3.5 on cont spreads values across the full [-1, 1] range with
    // healthy saturation at the tails (= consistent mountain plateaus and
    // deep ocean basins, rather than everything sitting near 0).
    auto stretch = [](double v, double gain) { return std::clamp(v * gain, -1.0, 1.0); };

    c.continentalness = stretch(
        octaveNoiseFrom(noiseCont, nx * CLIMATE_FREQ_CONT, nz * CLIMATE_FREQ_CONT, 5, 0.5, 2.0), 3.5);
    c.erosion = stretch(
        octaveNoiseFrom(noiseErosion, nx * CLIMATE_FREQ_EROSION, nz * CLIMATE_FREQ_EROSION, 4, 0.5, 2.0), 3.0);
    c.weirdness = stretch(
        octaveNoiseFrom(noiseWeird, nx * CLIMATE_FREQ_WEIRD, nz * CLIMATE_FREQ_WEIRD, 4, 0.5, 2.0), 2.5);
    c.temperature = stretch(
        octaveNoiseFrom(noiseTemp, nx * CLIMATE_FREQ_TEMP, nz * CLIMATE_FREQ_TEMP, 4, 0.5, 2.0), 2.5);
    c.humidity = stretch(
        octaveNoiseFrom(noiseHumid, nx * CLIMATE_FREQ_HUMID, nz * CLIMATE_FREQ_HUMID, 4, 0.5, 2.0), 2.5);
    c.pv = 1.0 - std::abs(3.0 * std::abs(c.weirdness) - 2.0);
    return c;
}

double TerrainGenerator::getTemperature(int x, int y) {
    // Legacy API — normalised to [0, 1] for old callers.
    return (noiseTemp.noise(x * scale * CLIMATE_FREQ_TEMP, y * scale * CLIMATE_FREQ_TEMP) + 1.0) * 0.5;
}

double TerrainGenerator::getMoisture(int x, int y) {
    return (noiseHumid.noise(x * scale * CLIMATE_FREQ_HUMID, y * scale * CLIMATE_FREQ_HUMID) + 1.0) * 0.5;
}

// --- Splines ------------------------------------------------------------

double TerrainGenerator::splineBaseHeight(const Climate& c) {
    // Piecewise-linear spline: continentalness picks coarse elevation band,
    // erosion flattens, PV bumps peaks up or carves valleys down.
    double cont = c.continentalness;
    double base;
    // Continentalness → terrain-height spline. Shape copied from Henrik
    // Kniberg's talk (cont-height graph): gradual ocean-to-coast rise, a
    // sharp cliff near cont ≈ 0.3, then flat mountain plateau. The cliff is
    // what makes Minecraft's coasts feel like "real world" — mountains rise
    // dramatically from the water, not via a gentle ramp.
    if (cont < -0.455) {
        double t = (cont + 1.0) / 0.545;  // deep ocean: Y 20..36
        base = 20.0 + t * 16.0;
    } else if (cont < -0.19) {
        double t = (cont + 0.455) / 0.265;  // ocean: Y 36..54
        base = 36.0 + t * 18.0;
    } else if (cont < -0.11) {
        double t = (cont + 0.19) / 0.08;  // coast: Y 54..64
        base = 54.0 + t * 10.0;
    } else if (cont < 0.03) {
        double t = (cont + 0.11) / 0.14;  // near-inland: Y 64..70
        base = 64.0 + t * 6.0;
    } else if (cont < 0.30) {
        double t = (cont - 0.03) / 0.27;  // mid-inland: Y 70..80
        base = 70.0 + t * 10.0;
    } else if (cont < 0.42) {
        double t = (cont - 0.30) / 0.12;  // CLIFF: Y 80..108 over a tiny cont range
        // Smoothstep so the cliff has a nice S curve rather than a ramp.
        double s = t * t * (3.0 - 2.0 * t);
        base = 80.0 + s * 28.0;
    } else {
        double t = std::min(1.0, (cont - 0.42) / 0.58);  // plateau: Y 108..115
        base = 108.0 + t * 7.0;
    }

    // Erosion flattens inland terrain toward sea level. Ocean floors are
    // exempt — flattening them would push water-biome positions above sea.
    if (cont > -0.11) {
        double erosionPull = (c.erosion + 1.0) * 0.5;
        double settled = SEA_LEVEL + 4.0;
        base = base * (1.0 - erosionPull * 0.25) + settled * (erosionPull * 0.25);
    }

    // PV (peaks-and-valleys) reshape: adds ±12 blocks of ridge above
    // coastline, sharpening the difference between valley floors and
    // jagged peaks.
    // PV: sharpens peaks vs valleys on land. Skip over ocean so the floor
    // stays smooth.
    if (cont > -0.11) base += c.pv * 12.0;

    // Jagged ridges on high-cont + low-erosion terrain. The ridged noise
    // 1-|n| produces sharp crests where Perlin noise crosses zero — this is
    // what turns smooth mountain domes into saw-toothed ranges.
    if (cont > 0.30 && c.erosion < 0.0) {
        double ridgeStrength = std::min(1.0, (cont - 0.30) / 0.20) * (-c.erosion);
        // Ridge in local (x,z). Uses weirdness noise since it already has
        // peak-oriented characteristics; no extra PerlinNoise instance needed.
        // Note: c.pv already encodes a 1-based ridge of weirdness so this
        // adds a finer-scale ridge on top.
        double rx = c.weirdness * ridgeStrength * 8.0;
        base += std::abs(rx);  // pushes up only — never dips valleys on peaks
    }

    return std::clamp(base, 1.0, static_cast<double>(maxHeight - 1));
}

double TerrainGenerator::splineHeightFactor(const Climate& c) {
    // Oceans: small factor = smooth floor. Inland with low erosion: big
    // factor = dramatic peaks. Plains/meadows: middle.
    double cont = c.continentalness;
    double inland = std::clamp((cont + 0.15) / 0.85, 0.0, 1.0);  // 0 ocean .. 1 deep inland
    double erosionFlat = std::clamp((c.erosion + 1.0) * 0.5, 0.0, 1.0);
    // Low erosion + inland → big factor (up to 4.0); high erosion → 0.3.
    double factor = 0.3 + inland * (1.0 - erosionFlat) * 3.5;
    return factor;
}

double TerrainGenerator::density(int x, int y, int z, double baseH, double factor) {
    // Heightmap terrain: solid iff y < baseH + rough2D(x, z). Three layers
    // of 2D noise stacked onto the spline base:
    //   - mid-scale octave detail (~±3 blocks)  — rolling hills
    //   - low-freq ridge noise on mountains (up to +6) — jagged peaks
    //   - HIGH-freq low-amp hash noise (~±1 block) — block-scale bumpiness
    //     so no slope is ever a perfectly smooth ramp
    (void)z;
    double nx = x * scale, nz = z * scale;
    double detail = octaveNoiseFrom(noise3DA, nx * 0.04, nz * 0.04, 4, 0.5, 2.0);
    double ridge = 1.0 - std::abs(noise3DB.noise(nx * 0.02, nz * 0.02));
    ridge = ridge * ridge;
    // High-freq roughness. Single octave at ~6× the detail frequency, small
    // amplitude — purely to break up smooth ramps into block-scale wobble.
    double rough = perlinNoise.noise(nx * 0.25, nz * 0.25) * 0.6 +
                   perlinNoise.noise(nx * 0.5, nz * 0.5) * 0.3;
    double jitter = detail * 3.5 + ridge * factor * 1.8 + rough * 2.0;
    return (baseH + jitter) - y;
}

// --- Caves --------------------------------------------------------------
// inCave is intentionally a no-op now: cave carving was pulling too much
// material out of the terrain (holes in mountain flanks, tunnels through
// plains) and the user preferred solid ground. Leaving the function in
// place so callers (density/scan-down and chunk.cpp phase 2) keep compiling.
#if 0
bool TerrainGenerator::inCave_original(int x, int y, int z) {
    if (y <= 2 || y >= maxHeight - 4) return false;  // no caves in bedrock / near surface cap
    double nx = x * scale, ny = y * scale, nz = z * scale;
    // Cheese cave: large pockets where noise is far from zero.
    double cheese = noiseCaveCheese.noise(nx * CAVE_FREQ, ny * CAVE_FREQ * 1.6, nz * CAVE_FREQ);
    if (std::abs(cheese) < 0.08) return true;  // near-zero contour = carved
    // Spaghetti cave: thin tunnels where two noise slabs intersect.
    double sA = noiseCaveSpaghetti.noise(nx * CAVE_FREQ * 0.8 + 100.0, ny * CAVE_FREQ * 1.4, nz * CAVE_FREQ * 0.8);
    double sB = noiseCaveSpaghetti.noise(nx * CAVE_FREQ * 0.8, ny * CAVE_FREQ * 1.4, nz * CAVE_FREQ * 0.8 + 100.0);
    if (std::abs(sA) < 0.05 && std::abs(sB) < 0.05) return true;
    return false;
}
#endif

bool TerrainGenerator::inCave(int /*x*/, int /*y*/, int /*z*/) {
    return false;
}

bool TerrainGenerator::oreHit(int x, int y, int z) {
    if (y < 4 || y > 90) return false;  // coal band
    double nx = x * scale, ny = y * scale, nz = z * scale;
    double n = perlinNoise.noise(nx * ORE_FREQ, ny * ORE_FREQ, nz * ORE_FREQ);
    return n > 0.55;
}

// --- Biome picker -------------------------------------------------------

Biome TerrainGenerator::pickBiome(const Climate& c, double heightAboveSea) {
    double cont = c.continentalness;
    double temp = c.temperature;
    double humid = c.humidity;
    double erosion = c.erosion;
    double pv = c.pv;

    // Minecraft's quantized parameter levels. Temperature and humidity split
    // the climate space into a 5×5 grid; continentalness picks the ocean →
    // coast → inland tier; erosion decides flat vs mountainous; PV shapes
    // plateaus vs valleys. The parameter ranges below come straight from the
    // wiki (see the user-supplied spec for sources).
    auto tLevel = [](double t) {  // 0..4, colder → warmer
        if (t < -0.45) return 0;
        if (t < -0.15) return 1;
        if (t < 0.20) return 2;
        if (t < 0.55) return 3;
        return 4;
    };
    auto hLevel = [](double h) {  // 0..4, drier → wetter
        if (h < -0.35) return 0;
        if (h < -0.10) return 1;
        if (h < 0.10) return 2;
        if (h < 0.30) return 3;
        return 4;
    };
    enum ContBand { CONT_DEEP_OCEAN, CONT_OCEAN, CONT_COAST, CONT_NEAR, CONT_MID, CONT_FAR };
    auto contBand = [](double v) -> ContBand {
        if (v < -0.455) return CONT_DEEP_OCEAN;
        if (v < -0.190) return CONT_OCEAN;
        if (v < -0.110) return CONT_COAST;
        if (v < 0.030) return CONT_NEAR;
        if (v < 0.300) return CONT_MID;
        return CONT_FAR;
    };
    enum PvBand { PV_VALLEY, PV_LOW, PV_MID, PV_HIGH, PV_PEAK };
    auto pvBand = [](double v) -> PvBand {
        if (v < -0.85) return PV_VALLEY;
        if (v < -0.20) return PV_LOW;
        if (v < 0.20) return PV_MID;
        if (v < 0.70) return PV_HIGH;
        return PV_PEAK;
    };
    // Erosion levels mostly matter for inland variant selection; we only use
    // three coarse buckets (low = mountain, mid = normal, high = shattered).
    bool eLow = erosion < -0.375;
    bool eHigh = erosion > 0.55;

    int T = tLevel(temp);
    int H = hLevel(humid);
    ContBand CB = contBand(cont);
    PvBand PVB = pvBand(pv);

    // Non-inland tiers: ocean / deep ocean / coast — climate and terrain
    // don't matter here, Minecraft treats these as purely continentalness.
    if (CB == CONT_DEEP_OCEAN) return BIOME_DEEP_OCEAN;
    if (CB == CONT_OCEAN) return BIOME_OCEAN;

    // Coast tier: sandy beach warm, snowy beach cold, stone shore on steep
    // low-erosion coasts. River: valleys that cut through inland.
    if (PVB == PV_VALLEY && heightAboveSea < 2.0) return BIOME_RIVER;
    if (CB == CONT_COAST || heightAboveSea < 2.0) {
        if (T == 0) return BIOME_SNOWY_BEACH;
        if (eLow) return BIOME_STONE_SHORE;
        return BIOME_BEACH;
    }

    // Inland. High altitude + low erosion = peaks; high erosion = shattered
    // windswept; moderate erosion at plateau height = meadow; otherwise
    // the 5×5 middle matrix drives biome choice.
    if (heightAboveSea > 42.0 && eLow) {
        if (T <= 1) return BIOME_SNOWY_PEAKS;
        return BIOME_STONY_PEAKS;
    }
    if (heightAboveSea > 28.0 && eHigh) return BIOME_WINDSWEPT_HILLS;
    if (heightAboveSea > 22.0 && PVB >= PV_HIGH) return BIOME_MEADOW;

    // Swamp: wet + warm + low terrain.
    if (H >= 4 && T >= 2 && heightAboveSea < 10.0) return BIOME_SWAMP;

    // Middle-biome 5×5 lookup (T × H) collapsed to our block-palette biome set.
    // Cells that would normally produce biomes we don't support (jungle,
    // savanna, badlands, etc.) fall back to the nearest neighbour in T/H.
    static constexpr Biome MIDDLE[5][5] = {
        //  T=0            T=1            T=2            T=3            T=4
        { BIOME_TUNDRA,  BIOME_PLAINS,  BIOME_PLAINS,  BIOME_PLAINS,  BIOME_DESERT }, // H=0
        { BIOME_TUNDRA,  BIOME_PLAINS,  BIOME_PLAINS,  BIOME_PLAINS,  BIOME_DESERT }, // H=1
        { BIOME_TUNDRA,  BIOME_TAIGA,   BIOME_FOREST,  BIOME_FOREST,  BIOME_PLAINS }, // H=2
        { BIOME_TUNDRA,  BIOME_TAIGA,   BIOME_FOREST,  BIOME_FOREST,  BIOME_FOREST }, // H=3
        { BIOME_TAIGA,   BIOME_TAIGA,   BIOME_FOREST,  BIOME_FOREST,  BIOME_FOREST }, // H=4
    };
    return MIDDLE[H][T];
}

Biome TerrainGenerator::getBiome(int x, int z) {
    // Delegate to getHeightAndBiome so the "separate" legacy calls stay in
    // sync with the combined one. Callers that only need the biome pay the
    // same scan-down cost as anyone needing the height — this is a cold API.
    Biome b;
    getHeightAndBiome(x, z, b);
    return b;
}

const BiomeParams& TerrainGenerator::getBiomeParams(Biome b) { return BIOME_TABLE[b]; }

// --- Heightmap (scan-down) ---------------------------------------------

int TerrainGenerator::getHeight(int x, int z) {
    Climate c = sampleClimate(x, z);
    double baseH = splineBaseHeight(c);
    double factor = splineHeightFactor(c);
    // Scan down from maxHeight. Stop on first solid voxel. Skip caves so
    // the reported height is the outer terrain surface (not the inside of
    // an overhang).
    for (int y = maxHeight - 1; y > 0; --y) {
        if (density(x, y, z, baseH, factor) > 0.0 && !inCave(x, y, z)) return y;
    }
    return 1;
}

int TerrainGenerator::getHeightAndBiome(int x, int z, Biome& outBiome) {
    Climate c = sampleClimate(x, z);
    double baseH = splineBaseHeight(c);
    double factor = splineHeightFactor(c);
    int h = 1;
    for (int y = maxHeight - 1; y > 0; --y) {
        if (density(x, y, z, baseH, factor) > 0.0 && !inCave(x, y, z)) {
            h = y;
            break;
        }
    }
    outBiome = pickBiome(c, h - SEA_LEVEL);
    return h;
}

// --- Legacy noise wrappers (unchanged semantics) -----------------------

double TerrainGenerator::getNoise(int x, int y) {
    return perlinNoise.noise(x * scale, y * scale) + 0.5;
}

double TerrainGenerator::getNoise(int x, int y, int z) {
    return perlinNoise.noise(x * scale, y * scale, z * scale) + 0.5;
}
