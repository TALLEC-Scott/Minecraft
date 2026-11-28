// Tundra: snow-covered ground, sparse spruce, gravel shores. Also the
// snow caps of mountains anywhere (altitude cooling in pickBiome).

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

bool growTree(const biomegen::TreeSite& s) {
    return trees::spruce(s);
}

block_type groundCover(const biomegen::GroundSite& g) {
    const block_type ground = g.ground;
    const uint32_t roll = g.roll;
    return ground == GRASS && roll < 200 ? TALL_GRASS : AIR;
}

} // namespace

const BiomeDef TUNDRA_DEF = {
    "Tundra",
    SNOW, DIRT, GRAVEL,
    0.08f, &growTree,
    nullptr,
    &groundCover,
    {0.88f, 0.95f, 1.70f}, // grass tint
    {0.81f, 0.94f, 1.95f}, // foliage tint
};
