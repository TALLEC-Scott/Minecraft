// Beach: the sand strip between ocean and land.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

block_type groundCover(const biomegen::GroundSite& g) {
    // The odd grass column at the back of the beach: a thin share of the
    // climate's grass.
    return g.ground == GRASS ? biomegen::climateGrass(g, 0.3, 0.5) : AIR;
}

} // namespace

const BiomeDef BEACH_DEF = {
    "Beach",
    SAND, SAND, SAND,
    0.0f, nullptr,
    nullptr,
    &groundCover,
    {1.0f, 1.0f, 1.0f}, // grass tint
    {1.0f, 1.0f, 1.0f}, // foliage tint
};
