#include "biome.h"

#include <algorithm>
#include <cmath>

#include "biome_defs.h"

namespace {

// One row per Biome, in enum order.
const BiomeDef* const BIOME_DEFS[] = {
    &OCEAN_DEF,           // BIOME_OCEAN
    &BEACH_DEF,           // BIOME_BEACH
    &PLAINS_DEF,          // BIOME_PLAINS
    &FOREST_DEF,          // BIOME_FOREST
    &DESERT_DEF,          // BIOME_DESERT
    &TUNDRA_DEF,          // BIOME_TUNDRA
    &TAIGA_DEF,           // BIOME_TAIGA
    &RAINFOREST_DEF,      // BIOME_RAINFOREST
    &SEASONAL_FOREST_DEF, // BIOME_SEASONAL_FOREST
    &SWAMPLAND_DEF,       // BIOME_SWAMPLAND
    &SAVANNA_DEF,         // BIOME_SAVANNA
    &SHRUBLAND_DEF,       // BIOME_SHRUBLAND
};
static_assert(sizeof(BIOME_DEFS) / sizeof(BIOME_DEFS[0]) == BIOME_COUNT, "one BiomeDef per Biome, in enum order");

} // namespace

const BiomeDef& biomeDef(Biome b) {
    return *BIOME_DEFS[b];
}

// The climate table, after Minecraft Beta's (temperature × humidity), with
// thresholds tuned to this generator's climate noise (both roughly normal
// around 0.5, sd ~0.16). Neighbouring cells of the table are biomes that
// look right next to each other, since temperature and humidity change
// smoothly across the world:
//
//              dry  ─────────── humidity ───────────►  wet
//   cold       tundra (altitude-cooled temperature, so also mountain caps)
//   cool       taiga
//   temperate  shrubland │ plains   │ forest         │ swampland (low ground)
//   warm       savanna   │ plains   │ seasonal forest │ rainforest
//   hot        desert    │ savanna  │ seasonal forest │ rainforest
Biome pickBiome(const Climate& c) {
    if (c.cooled < 0.22) return BIOME_TUNDRA;
    // Cool regions, and the band below the snow caps on high ground, so
    // mountains go forest -> taiga -> tundra on the way up.
    if (c.temperature < 0.37 || c.cooled < 0.29) return BIOME_TAIGA;
    const double t = c.temperature, h = c.humidity;
    if (t < 0.58) {
        if (h < 0.36) return BIOME_SHRUBLAND;
        if (h < 0.50) return BIOME_PLAINS;
        // Swamps only on low, flat ground; wet hills stay forest.
        if (h >= 0.60 && c.heightAboveSea <= 9 && c.relief < 10.0) return BIOME_SWAMPLAND;
        return BIOME_FOREST;
    }
    if (t < 0.66) {
        if (h < 0.38) return BIOME_SAVANNA;
        if (h < 0.52) return BIOME_PLAINS;
        if (h < 0.64) return BIOME_SEASONAL_FOREST;
        return BIOME_RAINFOREST;
    }
    // Hot. Mountain relief suppresses desert so peaks don't read as dunes;
    // those become savanna highlands instead.
    if (h < 0.45) return c.relief < 14.0 ? BIOME_DESERT : BIOME_SAVANNA;
    if (h < 0.55) return BIOME_SAVANNA;
    if (h < 0.63) return BIOME_SEASONAL_FOREST;
    return BIOME_RAINFOREST;
}

// --- Climate-driven grass and colour ---

namespace {

double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

uint64_t mix64(uint64_t v) {
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

uint64_t hashAt(uint64_t seed, int x, int z, uint64_t salt) {
    return mix64(seed * 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^
                 static_cast<uint32_t>(z) ^ (salt * 0xD1B54A32D192ED03ULL));
}

double unit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Smoothed value noise on a `cell`-block lattice, 0..1.
double valueNoise(uint64_t seed, int wx, int wz, int cell, uint64_t salt) {
    const int cx = floorDiv(wx, cell), cz = floorDiv(wz, cell);
    auto fade = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double fx = fade(static_cast<double>(wx - cx * cell) / cell);
    const double fz = fade(static_cast<double>(wz - cz * cell) / cell);
    const double a = unit(hashAt(seed, cx, cz, salt)), b = unit(hashAt(seed, cx + 1, cz, salt));
    const double c = unit(hashAt(seed, cx, cz + 1, salt)), d = unit(hashAt(seed, cx + 1, cz + 1, salt));
    return (a + (b - a) * fx) * (1.0 - fz) + (c + (d - c) * fx) * fz;
}

constexpr uint64_t SALT_GRASS = 0x6A55C0DEULL, SALT_TALL_CLUMP = 0x6A55C1DEULL;

// Each biome's typical climate (the middle of its cell in pickBiome's
// table; tundra's temperature is the cooled one), where climateTint leaves
// its tint as drawn. In enum order.
struct Typical {
    double temperature, humidity;
};
constexpr Typical TYPICAL[] = {
    {0.50, 0.50}, // OCEAN
    {0.50, 0.50}, // BEACH
    {0.56, 0.45}, // PLAINS
    {0.47, 0.58}, // FOREST
    {0.74, 0.36}, // DESERT
    {0.17, 0.50}, // TUNDRA
    {0.31, 0.50}, // TAIGA
    {0.68, 0.72}, // RAINFOREST
    {0.66, 0.58}, // SEASONAL_FOREST
    {0.48, 0.68}, // SWAMPLAND
    {0.66, 0.38}, // SAVANNA
    {0.48, 0.28}, // SHRUBLAND
};
static_assert(sizeof(TYPICAL) / sizeof(TYPICAL[0]) == BIOME_COUNT, "one typical climate per Biome, in enum order");

} // namespace

float grassDensity(double temperature, double humidity) {
    // Humidity sets the amount: ~10% of columns when dry, ~65% when lush.
    const double wet = 0.10 + 0.55 * smoothstep(0.10, 0.80, humidity);
    // Cold thins it out to nothing at the snow line (tundra < 0.22).
    const double warmth = smoothstep(0.16, 0.40, temperature);
    // Hot and dry (desert) leaves only stragglers.
    const double desert = smoothstep(0.58, 0.80, temperature) * (1.0 - smoothstep(0.32, 0.58, humidity));
    return static_cast<float>(wet * warmth * (1.0 - 0.95 * desert));
}

float tallGrassShare(double temperature, double humidity) {
    const double warm = smoothstep(0.35, 0.60, temperature);
    const double notWet = 1.0 - smoothstep(0.50, 0.75, humidity);
    return static_cast<float>(0.04 + 0.28 * warm * notWet);
}

glm::vec3 climateTint(Biome b, double temperature, double humidity, bool foliage) {
    const BiomeDef& def = biomeDef(b);
    const Typical& t0 = TYPICAL[b];
    const double dt = std::clamp(temperature - t0.temperature, -0.25, 0.25);
    const double dh = std::clamp(humidity - t0.humidity, -0.25, 0.25);
    // Log-space channel shifts: warmer / drier pushes red up and blue down
    // (straw), wetter greens and cools, colder pales toward blue.
    const double amp = foliage ? 0.8 : 1.0;
    const glm::vec3 shift(static_cast<float>(std::exp(amp * (1.10 * dt - 1.30 * dh))),
                          static_cast<float>(std::exp(amp * (0.15 * dt + 0.25 * dh))),
                          static_cast<float>(std::exp(amp * (-1.30 * dt + 0.80 * dh))));
    return (foliage ? def.foliageTint : def.grassTint) * shift;
}

namespace biomegen {

double groundRoll(const GroundSite& g, uint64_t salt) {
    return unit(hashAt(g.seed, g.wx, g.wz, salt));
}

block_type climateGrass(const GroundSite& g, double amount, double tallScale) {
    const double density = std::min(0.95, static_cast<double>(grassDensity(g.temperature, g.humidity)) * amount);
    const double r = groundRoll(g, SALT_GRASS);
    if (r >= density) return AIR;
    // Two-block grass in clumps: x0.3 .. x1.7 over ~7 blocks.
    const double clump = 0.3 + 1.4 * valueNoise(g.seed, g.wx, g.wz, 7, SALT_TALL_CLUMP);
    const double tall = std::clamp(static_cast<double>(tallGrassShare(g.temperature, g.humidity)) * tallScale * clump, 0.0, 1.0);
    return r < density * tall ? DOUBLE_TALL_GRASS : TALL_GRASS;
}

} // namespace biomegen
