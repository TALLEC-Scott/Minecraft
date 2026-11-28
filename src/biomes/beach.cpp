// Beach: the sand strip between ocean and land.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

block_type groundCover(block_type ground, uint32_t roll) {
    return ground == GRASS && roll < 200 ? TALL_GRASS : AIR;
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
