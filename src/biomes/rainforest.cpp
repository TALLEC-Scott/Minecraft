// Rainforest: hot and wet. Placeholder until its own design lands:
// dense oaks, lush tint.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(block_type ground, uint32_t roll) {
    if (ground != GRASS) return AIR;
    return roll < 2500 ? TALL_GRASS : roll < 3000 ? FERN : AIR;
}

} // namespace

const BiomeDef RAINFOREST_DEF = {
    "Rainforest",
    GRASS, DIRT, SAND,
    0.70f, &growTree,
    nullptr,
    &groundCover,
    {0.61f, 1.06f, 0.67f}, // grass tint
    {0.40f, 1.09f, 0.23f}, // foliage tint
};
