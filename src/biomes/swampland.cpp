// Swampland: low, wet, murky. Placeholder until its own design lands.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(block_type ground, uint32_t roll) {
    return ground == GRASS && roll < 1200 ? TALL_GRASS : AIR;
}

} // namespace

const BiomeDef SWAMPLAND_DEF = {
    "Swampland",
    GRASS, DIRT, DIRT,
    0.20f, &growTree,
    nullptr,
    &groundCover,
    {0.73f, 0.59f, 0.64f}, // grass tint
    {0.89f, 0.66f, 1.21f}, // foliage tint
};
