//
// Created by scott on 03/07/23.
//
// Minecraft 1.18-style terrain shaping: low-frequency climate noises
// (continentalness, erosion) plus a ridged "mountain" noise are mapped
// through splines to a terrain height, instead of noise driving height
// directly. See https://www.alanzucconi.com/2022/06/05/minecraft-world-generation/
//
//   height = seaLevel + offset(C) + relief + cliffs + detail, then rivers
//
// - offset(C):  base elevation by distance from coast (ocean floor
//               plateau, sharp coastline, inland plains, highlands).
// - relief:     inlandness · amp(E) · ridge. amp(E) is mountain amplitude
//               by erosion (heavily eroded land stays flat); ridge is
//               ridged multifractal noise — 1 along connected crest
//               lines, falling off into valleys — so mountains form
//               ranges instead of the rings a folded-noise PV produced.
// - cliffs:     in low-erosion (jagged) land the height is pulled toward
//               a terraced version of itself: flat benches separated by
//               steep risers, i.e. real cliffs.
// - inlandness: 0 at sea, ramps to 1 inland, so mountains never grow
//               out of the ocean or the beach.
// - rivers:     a separate low-frequency noise; along its zero line the
//               land is carved down to a channel a few blocks below sea
//               level, with sloped banks, so rivers fill with water.
#include "TerrainGenerator.h"
#include "biome.h"
#include <algorithm>
#include <cmath>

namespace {

struct SplinePt {
    double x, y;
};

// Piecewise interpolation through control points with smoothstep easing
// between neighbors. Smoothstep (rather than plain lerp) gives C1-ish
// continuity at the knots without cubic overshoot — outputs never leave
// the [min, max] of the surrounding control values.
template <int N> double splineEval(const SplinePt (&pts)[N], double x) {
    if (x <= pts[0].x) return pts[0].y;
    if (x >= pts[N - 1].x) return pts[N - 1].y;
    int i = 0;
    while (x > pts[i + 1].x) i++;
    double t = (x - pts[i].x) / (pts[i + 1].x - pts[i].x);
    t = t * t * (3.0 - 2.0 * t);
    return pts[i].y + t * (pts[i + 1].y - pts[i].y);
}

double smoothstep(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Base elevation (blocks relative to sea level) by continentalness.
// The steep -0.19 → -0.11 segment is the coastline cliff; the long
// flat middle keeps most land near sea level like Minecraft's plains.
constexpr SplinePt OFFSET_SPLINE[] = {
    {-1.00, -30.0}, // deep ocean floor
    {-0.45, -16.0}, // ocean
    {-0.19, -7.0},  // shallows
    {-0.11, 1.0},   // coastline
    {0.03, 3.0},    // coastal plains
    {0.30, 9.0},    // inland
    {0.65, 18.0},   // far inland plateau
    {1.00, 26.0},   // highlands
};

// Mountain amplitude (blocks) by erosion. Low erosion → dramatic
// relief; high erosion → planed-flat terrain even far inland. Peaks
// reach the upper quarter of the 128-block world.
constexpr SplinePt AMP_SPLINE[] = {
    {-1.00, 62.0}, // jagged peaks
    {-0.60, 44.0},
    {-0.20, 22.0},
    {0.20, 9.0},
    {0.60, 4.0},
    {1.00, 2.0}, // near-flat
};

// Normalized FBM rarely leaves ±0.4 (octave averaging compresses the
// range), which would starve the spline endpoints — no deep oceans, no
// jagged-peak erosion. Stretch back toward the nominal -1..1 before the
// splines see the value.
double stretch(double n, double k) {
    return std::clamp(n * k, -1.0, 1.0);
}

// Continentalness thresholds shared by height shaping and biome pick.
constexpr double OCEAN_END = -0.11;  // below: ocean
constexpr double BEACH_END = -0.04;  // below: beach
constexpr double INLAND_FULL = 0.12; // mountains at full strength

// Cliffs: in jagged terrain the height is pulled toward terraces of this
// many blocks; the riser occupies the middle RISER fraction of each step.
constexpr double TERRACE_STEP = 7.0;
constexpr double TERRACE_RISER = 0.08;
// Escarpment step height, and the half-width (in noise units) of the
// transition: noise gradient is ~0.01/block, so 0.012 ≈ a 2-3 block wall.
constexpr double ESCARPMENT_HEIGHT = 9.0;
constexpr double ESCARPMENT_EDGE = 0.012;

// Rivers: distance-from-zero of the river noise below which the channel
// (core) and its banks are carved. Bed sits RIVER_DEPTH below sea level.
constexpr double RIVER_CORE = 0.005;
constexpr double RIVER_BANK = 0.016;

// Shifts continentalness toward land: unbiased, about half the world was
// one featureless ocean.
constexpr double LAND_BIAS = 0.08;
constexpr double RIVER_DEPTH = 3.0;

// Peaks are soft-capped so the tallest trees (≤ 13 blocks) still fit
// under the world ceiling.
constexpr double PEAK_SOFT_CAP = 108.0;

// Jaggedness: relief (blocks) over which overhangs fade in, share of the
// cliff mask that counts, the band above sea level over which they fade in,
// and how far past the river bank (× RIVER_BANK in noise units) they stay off.
// Coal: the noise cutoff falls by COAL_HIGH_DROP over COAL_RAMP blocks
// above sea level (1.3% → ~5% of stone).
constexpr double COAL_RAMP = 48.0;
constexpr double COAL_HIGH_DROP = 0.12;

constexpr double JAG_RELIEF_LO = 9.0;
constexpr double JAG_RELIEF_HI = 24.0;
constexpr double JAG_CLIFF_SHARE = 1.0;
constexpr double JAG_CLIFF_RELIEF_LO = 8.0;
constexpr double JAG_CLIFF_RELIEF_HI = 16.0;
constexpr double JAG_SEA_LO = 4.0;
constexpr double JAG_SEA_HI = 14.0;
constexpr double JAG_RIVER_CLEAR = 2.5;
constexpr int PEAK_HARD_CAP = 114;

} // namespace

TerrainGenerator::TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight)
    : perlinNoise(seed), seed(seed), scale(scale), minHeight(minHeight), maxHeight(maxHeight) {}

double TerrainGenerator::octaveNoise(double x, double y, int octaves, double persistence, double lacunarity) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        total += perlinNoise.noise(x * frequency, y * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxVal;
}

double TerrainGenerator::ridgedNoise(double x, double y, int octaves) {
    double total = 0.0, amplitude = 1.0, frequency = 1.0, maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        // 1 at the noise's zero line, 0 once |n| ≥ 0.45: thin crests that
        // join into ranges. Squaring sharpens the crest profile.
        double r = std::max(0.0, 1.0 - std::abs(perlinNoise.noise(x * frequency, y * frequency)) / 0.45);
        total += r * r * amplitude;
        maxVal += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return total / maxVal;
}

// Climate noises vary over ~800 blocks (5× the continent detail of the
// old 0.06 / 0.08 factors), so biomes form coherent regions instead of
// 150-block blobs; a faint high-frequency jitter roughens the borders.
double TerrainGenerator::getTemperature(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;
    double t = octaveNoise(nx * 0.012 + 2000.0, ny * 0.012 + 2000.0, 3, 0.5, 2.0) * 1.6;
    t += 0.09 * octaveNoise(nx * 0.15 + 2500.0, ny * 0.15 + 2500.0, 2, 0.5, 2.0);
    return std::clamp((t + 1.0) * 0.5, 0.0, 1.0);
}

double TerrainGenerator::getMoisture(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;
    double m = octaveNoise(nx * 0.016 + 1000.0, ny * 0.016 + 1000.0, 3, 0.5, 2.0) * 1.6;
    m += 0.09 * octaveNoise(nx * 0.15 + 1500.0, ny * 0.15 + 1500.0, 2, 0.5, 2.0);
    return std::clamp((m + 1.0) * 0.5, 0.0, 1.0);
}

TerrainGenerator::ColumnSample TerrainGenerator::sampleColumn(int x, int y, bool wantClimate, bool withBiome) {
    double nx = x * scale;
    double ny = y * scale;

    ColumnSample s{};
    // Climate noises. Distinct large offsets decorrelate them despite
    // sharing one permutation table (same trick as temperature/moisture).
    s.continentalness = std::clamp(stretch(octaveNoise(nx * 0.006, ny * 0.006, 4, 0.5, 2.0), 1.7) + LAND_BIAS, -1.0, 1.0);
    s.erosion = stretch(octaveNoise(nx * 0.015 + 3000.0, ny * 0.015 + 3000.0, 3, 0.5, 2.0), 1.9);
    s.ridge = ridgedNoise(nx * 0.03 + 4000.0, ny * 0.03 + 4000.0, 3);

    // --- Height ---
    int seaLevel = maxHeight / 2;
    double height = seaLevel + splineEval(OFFSET_SPLINE, s.continentalness);

    // Mountains fade in across the shore band so the beach stays flat
    // and the ocean floor never sprouts ridges.
    double inland = smoothstep(BEACH_END, INLAND_FULL, s.continentalness);
    double amp = splineEval(AMP_SPLINE, s.erosion);
    // Ridges carry the full amplitude; a gentle rolling term keeps the
    // land between ranges from being dead flat.
    double rolling = 0.18 * octaveNoise(nx * 0.06 + 6000.0, ny * 0.06 + 6000.0, 2, 0.5, 2.0);
    s.relief = inland * amp * (s.ridge + rolling);
    height += s.relief;

    // Cliffs: jagged (low-erosion) land gets escarpments — the ground steps
    // up ESCARPMENT_HEIGHT blocks across the zero line of a separate noise,
    // over just 2-3 blocks horizontally — and is pulled toward terraces,
    // which sharpens slopes that already exist into benches and risers.
    double cliffMask = inland * smoothstep(0.05, -0.3, s.erosion);
    if (cliffMask > 0.0) {
        double e = octaveNoise(nx * 0.12 + 6500.0, ny * 0.12 + 6500.0, 2, 0.5, 2.0);
        height += cliffMask * ESCARPMENT_HEIGHT * smoothstep(-ESCARPMENT_EDGE, ESCARPMENT_EDGE, e);
        double base = std::floor(height / TERRACE_STEP) * TERRACE_STEP;
        double f = (height - base) / TERRACE_STEP;
        double lo = 0.5 - TERRACE_RISER * 0.5;
        double terraced = base + TERRACE_STEP * smoothstep(lo, 1.0 - lo, f);
        height += (terraced - height) * cliffMask * 0.9;
    }

    // Small-scale surface detail, everywhere (dunes, ocean-floor ripple).
    height += octaveNoise(nx * 0.35, ny * 0.35, 3, 0.45, 2.0) * 2.0;

    // Rivers: carve along the zero line of a low-frequency noise, on land
    // only. The bank blend slopes down to sea level, the core drops to the
    // bed so the channel holds water.
    // Fades in from the coastline itself (not the inland edge of the beach)
    // so channels cut through the beach and actually reach the sea.
    double landMask = smoothstep(OCEAN_END, OCEAN_END + 0.03, s.continentalness);
    double riverFar = 1.0; // 0 in and beside a river channel, 1 away from it
    if (landMask > 0.0) {
        double rd = std::abs(octaveNoise(nx * 0.005 + 5000.0, ny * 0.005 + 5000.0, 2, 0.5, 2.0));
        riverFar = smoothstep(RIVER_BANK, RIVER_BANK * JAG_RIVER_CLEAR, rd);
        double bank = (1.0 - smoothstep(RIVER_CORE, RIVER_BANK, rd)) * landMask;
        if (bank > 0.0) {
            double core = 1.0 - smoothstep(RIVER_CORE * 0.4, RIVER_CORE * 1.4, rd);
            double bed = seaLevel + 1.0 - (RIVER_DEPTH + 1.0) * core;
            height += (std::min(height, bed) - height) * bank;
        }
    }

    if (height > PEAK_SOFT_CAP) height = PEAK_SOFT_CAP + (height - PEAK_SOFT_CAP) * 0.4;
    s.height = std::clamp(static_cast<int>(height), minHeight, std::min(maxHeight, PEAK_HARD_CAP));

    // --- Jaggedness (3D overhang strength, terrain_density.h) ---
    // Mountains (high relief) and cliff land (low erosion) get it; plains,
    // rolling hills, river corridors and anything near sea level don't.
    double jag = std::max(smoothstep(JAG_RELIEF_LO, JAG_RELIEF_HI, s.relief), JAG_CLIFF_SHARE * cliffMask * smoothstep(JAG_CLIFF_RELIEF_LO, JAG_CLIFF_RELIEF_HI, s.relief));
    jag *= riverFar * smoothstep(seaLevel + JAG_SEA_LO, seaLevel + JAG_SEA_HI, static_cast<double>(s.height));
    s.jagged = jag < 0.02 ? 0.0f : static_cast<float>(jag);

    if (!withBiome) return s;

    // --- Biome ---
    // Altitude cooling: high ground is colder, so tall ranges get tundra
    // (snowy) tops inside otherwise temperate regions. Only the tundra test
    // uses it (see pickBiome); vegetation reads it as the column's climate.
    auto climate = [&] {
        Climate c{};
        c.temperature = getTemperature(x, y);
        c.humidity = getMoisture(x, y);
        double altitude = (s.height - seaLevel) / (double)seaLevel;
        c.cooled = c.temperature - std::max(0.0, altitude) * 0.45;
        c.relief = s.relief;
        c.heightAboveSea = s.height - seaLevel;
        s.climate = {c.cooled, c.humidity};
        return c;
    };
    if (s.continentalness < OCEAN_END) {
        s.biome = BIOME_OCEAN;
        if (wantClimate) climate();
    } else if (s.continentalness < BEACH_END) {
        s.biome = BIOME_BEACH;
        if (wantClimate) climate();
    } else {
        s.biome = pickBiome(climate());
    }
    return s;
}

TerrainGenerator::ColumnShape TerrainGenerator::getShape(int x, int z) {
    ColumnSample s = sampleColumn(x, z, false, false);
    return {s.height, s.jagged};
}

int TerrainGenerator::getHeightAndBiome(int x, int z, Biome& outBiome, float& outJagged) {
    ColumnSample s = sampleColumn(x, z);
    outBiome = s.biome;
    outJagged = s.jagged;
    return s.height;
}

double TerrainGenerator::overhangNoise(int x, int y, int z) {
    // Horizontal wavelength ~56 blocks, vertical ~20: big blobs flattened
    // into thick shelves, so the ground juts out in slabs and is cut into
    // deep notches rather than round holes. The second octave roughens them
    // into crags.
    double n = perlinNoise.noise(x * 0.018 + 11000.0, y * 0.05 + 11000.0, z * 0.018 + 11000.0);
    n += 0.45 * perlinNoise.noise(x * 0.045 + 12000.0, y * 0.11 + 12000.0, z * 0.045 + 12000.0);
    return std::clamp(n * 1.25, -1.0, 1.0);
}

Biome TerrainGenerator::getBiome(int x, int y) {
    return sampleColumn(x, y).biome;
}

int TerrainGenerator::getHeight(int x, int y) {
    return sampleColumn(x, y).height;
}

int TerrainGenerator::getHeightBiomeClimate(int x, int y, Biome& outBiome, ColumnClimate* climate, float* jagged) {
    ColumnSample s = sampleColumn(x, y, climate != nullptr);
    outBiome = s.biome;
    if (climate) *climate = s.climate;
    if (jagged) *jagged = s.jagged;
    return s.height;
}

int TerrainGenerator::getHeightAndBiome(int x, int y, Biome& outBiome) {
    ColumnSample s = sampleColumn(x, y);
    outBiome = s.biome;
    return s.height;
}

// Two Minecraft-style cave families, both from 3D Perlin noise:
//
// - "Cheese" caves: one low-frequency 3D noise thresholded high — the
//   rare pockets where it exceeds the cutoff become large open caverns
//   (the holes in the cheese).
// - "Spaghetti" caves: two independent mid-frequency 3D noises. Each
//   noise's zero-surface is a 2D sheet winding through space; where the
//   TWO sheets intersect (both |n| small) you get a 1D winding tube —
//   a long tunnel a few blocks wide.
//
// The vertical frequency is higher than the horizontal so caverns and
// tunnels stretch sideways rather than forming vertical chimneys.
bool TerrainGenerator::isCave(int wx, int wy, int wz) {
    // Spaghetti first: cheaper hit rate to reject on (2 single-octave
    // evals), and tunnels are the most common cave encounter.
    // Low frequency = few, long tunnels. The generous cutoff keeps the
    // bore 4-5 blocks — comfortably walkable — and stops the tunnel from
    // pinching apart where the two noise sheets cross at a shallow angle.
    // Tunnels also fatten slightly with depth, blending into the cavern
    // layer at the bottom.
    double depthWiden = 0.015 * std::clamp(1.0 - wy / 64.0, 0.0, 1.0);
    double tube = 0.085 + depthWiden; // half-thickness of a tunnel
    double s1 = perlinNoise.noise(wx * 0.020 + 700.0, wy * 0.030 + 700.0, wz * 0.020 + 700.0);
    if (std::abs(s1) < tube) {
        double s2 = perlinNoise.noise(wx * 0.020 + 1400.0, wy * 0.030 + 1400.0, wz * 0.020 + 1400.0);
        if (std::abs(s2) < tube) return true;
    }

    // Noodle caves: same intersection trick at ~2× frequency with a
    // thinner cutoff — cramped 1–2 block squeeze-ways that wind tightly.
    // A slow thickness modulator pinches them shut and reopens them so
    // they read as crawl spaces, not endless uniform pipes.
    // Regional: the slow modulator must be positive for noodles to exist
    // at all, so they come in patches (a noodle "system" you stumble
    // into) instead of speckling the whole underground.
    double noodleMod = perlinNoise.noise(wx * 0.01 + 3500.0, wy * 0.01 + 3500.0, wz * 0.01 + 3500.0);
    if (noodleMod > 0.05) {
        double noodleThk = 0.03 + 0.35 * (noodleMod - 0.05) * 0.1;
        double n1 = perlinNoise.noise(wx * 0.055 + 2800.0, wy * 0.075 + 2800.0, wz * 0.055 + 2800.0);
        if (std::abs(n1) < noodleThk) {
            double n2 = perlinNoise.noise(wx * 0.055 + 4200.0, wy * 0.075 + 4200.0, wz * 0.055 + 4200.0);
            if (std::abs(n2) < noodleThk) return true;
        }
    }

    // Cheese caverns: low frequency → large chambers; the second octave
    // roughens their walls. The threshold falls with depth, so near the
    // surface caverns are rare pockets while deep down they open into
    // proper rooms (Minecraft's cheese caves live at the bottom too).
    double depth01 = std::clamp(1.0 - wy / 64.0, 0.0, 1.0); // 0 at sea level, 1 at world floor
    // Steep depth bias: near the surface caverns barely exist; the big
    // rooms live in the bottom third of the world.
    double thr = 0.66 - 0.38 * depth01;
    double c = perlinNoise.noise(wx * 0.009 + 2100.0, wy * 0.016 + 2100.0, wz * 0.009 + 2100.0);
    // The wall-roughening octave contributes at most ±ROUGH — skip it for
    // the vast majority of blocks that can't reach the threshold anyway.
    // Kept modest: a strong octave flips isolated blocks past the threshold
    // far from any cavern, speckling the stone with 1-3 block air pockets.
    constexpr double ROUGH = 0.18;
    if (c + ROUGH < thr) return false;
    if (c - ROUGH > thr) return true;
    c += ROUGH * perlinNoise.noise(wx * 0.027 + 2100.0, wy * 0.048 + 2100.0, wz * 0.027 + 2100.0);
    return c > thr;
}

bool TerrainGenerator::caveEntranceAllowed(int x, int z) {
    // ~50-block patches; only the top of the noise range qualifies.
    return perlinNoise.noise(x * 0.02 + 7700.0, z * 0.02 + 7700.0) > 0.5;
}

bool TerrainGenerator::isCoalOre(int wx, int wy, int wz) {
    // Blobs a few blocks across: thresholding a mid-frequency 3D noise
    // high keeps only isolated peaks, unlike the old near-zero band that
    // traced thin contour shells through all the stone.
    // Like modern Minecraft, coal gets commoner with altitude: ~1.3% of
    // stone at and below sea level, rising smoothly to ~5% over the next
    // COAL_RAMP blocks; the lower cutoff also grows the blobs into veins of
    // a dozen or so blocks, so mountains and cliffs are freckled with it.
    const double up = smoothstep(getSeaLevel(), getSeaLevel() + COAL_RAMP, static_cast<double>(wy));
    return perlinNoise.noise(wx * 0.19 + 9100.0, wy * 0.19 + 9100.0, wz * 0.19 + 9100.0) > 0.57 - COAL_HIGH_DROP * up;
}

bool TerrainGenerator::isIronOre(int wx, int wy, int wz) {
    if (wy > IRON_MAX_Y) return false;
    return perlinNoise.noise(wx * 0.23 + 9400.0, wy * 0.23 + 9400.0, wz * 0.23 + 9400.0) > 0.6;
}

bool TerrainGenerator::isDiamondOre(int wx, int wy, int wz) {
    if (wy > DIAMOND_MAX_Y) return false;
    return perlinNoise.noise(wx * 0.3 + 9700.0, wy * 0.3 + 9700.0, wz * 0.3 + 9700.0) > 0.66;
}

block_type TerrainGenerator::oreAt(int wx, int wy, int wz) {
    if (isCoalOre(wx, wy, wz)) return COAL_ORE;
    if (isIronOre(wx, wy, wz)) return IRON_ORE;
    if (isDiamondOre(wx, wy, wz)) return DIAMOND_ORE;
    return STONE;
}

int TerrainGenerator::soilDepth(int x, int z) {
    double n = perlinNoise.noise(x * 0.07 + 8300.0, z * 0.07 + 8300.0); // ~ -0.7..0.7
    return 3 + std::clamp(static_cast<int>((n + 0.7) / 1.4 * 3.0), 0, 2);
}

double TerrainGenerator::getNoise(int x, int y) {
    double noise = perlinNoise.noise(x * scale, y * scale) + 0.5;
    return noise;
}
