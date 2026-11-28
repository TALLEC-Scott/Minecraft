// Plains: open grassland, flowers, the odd lone oak. Its colours are the
// stock textures' (tint 1,1,1).

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(block_type ground, uint32_t roll) {
    if (ground != GRASS) return AIR;
    return roll < 1000 ? TALL_GRASS : roll < 1100 ? DANDELION : roll < 1200 ? POPPY : AIR;
}

} // namespace

const BiomeDef PLAINS_DEF = {
    "Plains",
    GRASS, DIRT, SAND,
    0.05f, &growTree,
    nullptr,
    &groundCover,
    {1.0f, 1.0f, 1.0f}, // grass tint
    {1.0f, 1.0f, 1.0f}, // foliage tint
};
