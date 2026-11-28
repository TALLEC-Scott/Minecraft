// Savanna: hot, dry grassland with scattered trees. Placeholder until its
// own design lands.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(const biomegen::GroundSite& g) {
    const block_type ground = g.ground;
    const uint32_t roll = g.roll;
    return ground == GRASS && roll < 1500 ? TALL_GRASS : AIR;
}

} // namespace

const BiomeDef SAVANNA_DEF = {
    "Savanna",
    GRASS, DIRT, SAND,
    0.04f, &growTree,
    nullptr,
    &groundCover,
    {1.32f, 0.97f, 0.95f}, // grass tint
    {1.46f, 0.96f, 0.89f}, // foliage tint
};
