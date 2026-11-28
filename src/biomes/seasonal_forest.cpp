// Seasonal forest: warm, fairly wet woods. Placeholder until its own
// design lands.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(block_type ground, uint32_t roll) {
    if (ground != GRASS) return AIR;
    return roll < 1000 ? TALL_GRASS : roll < 1100 ? POPPY : AIR;
}

} // namespace

const BiomeDef SEASONAL_FOREST_DEF = {
    "Seasonal Forest",
    GRASS, DIRT, SAND,
    0.40f, &growTree,
    nullptr,
    &groundCover,
    {0.90f, 1.03f, 0.90f}, // grass tint
    {0.84f, 1.02f, 0.96f}, // foliage tint
};
