// Shrubland: temperate but dry; low bushes. Placeholder until its own
// design lands.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::oak(s);
}

block_type groundCover(const biomegen::GroundSite& g) {
    const block_type ground = g.ground;
    const uint32_t roll = g.roll;
    return ground == GRASS && roll < 600 ? TALL_GRASS : AIR;
}

} // namespace

const BiomeDef SHRUBLAND_DEF = {
    "Shrubland",
    GRASS, DIRT, SAND,
    0.06f, &growTree,
    nullptr,
    &groundCover,
    {1.14f, 0.95f, 0.95f}, // grass tint
    {1.18f, 0.96f, 0.96f}, // foliage tint
};
