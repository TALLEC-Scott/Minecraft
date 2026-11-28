// Forest: dense oak woods with birch mixed in (the old birch-forest biome
// folded in here).

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return (s.bits >> 40) % 10 < 3 ? trees::birch(s) : trees::oak(s);
}

block_type groundCover(block_type ground, uint32_t roll) {
    if (ground != GRASS) return AIR;
    return roll < 800 ? TALL_GRASS : roll < 850 ? POPPY : roll < 900 ? DANDELION : AIR;
}

} // namespace

const BiomeDef FOREST_DEF = {
    "Forest",
    GRASS, DIRT, SAND,
    0.60f, &growTree,
    nullptr,
    &groundCover,
    {0.83f, 1.02f, 1.01f}, // grass tint
    {0.75f, 1.02f, 1.02f}, // foliage tint
};
