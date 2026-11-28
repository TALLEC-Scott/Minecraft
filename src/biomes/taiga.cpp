// Taiga: cool spruce woods with ferns.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::spruce(s);
}

block_type groundCover(const biomegen::GroundSite& g) {
    const block_type ground = g.ground;
    const uint32_t roll = g.roll;
    if (ground != GRASS) return AIR;
    return roll < 600 ? FERN : roll < 1000 ? TALL_GRASS : AIR;
}

} // namespace

const BiomeDef TAIGA_DEF = {
    "Taiga",
    GRASS, DIRT, SAND,
    0.45f, &growTree,
    nullptr,
    &groundCover,
    {0.92f, 0.97f, 1.47f}, // grass tint
    {0.87f, 0.96f, 1.90f}, // foliage tint
};
