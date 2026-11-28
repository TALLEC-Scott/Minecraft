#include "biome.h"

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
    &JUNGLE_DEF,          // BIOME_JUNGLE
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
//   warm       savanna   │ plains   │ seasonal forest │ jungle
//   hot        desert    │ savanna  │ seasonal forest │ jungle
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
        return BIOME_JUNGLE;
    }
    // Hot. Mountain relief suppresses desert so peaks don't read as dunes;
    // those become savanna highlands instead.
    if (h < 0.45) return c.relief < 14.0 ? BIOME_DESERT : BIOME_SAVANNA;
    if (h < 0.55) return BIOME_SAVANNA;
    if (h < 0.63) return BIOME_SEASONAL_FOREST;
    return BIOME_JUNGLE;
}
