// Ocean: sand and gravel floor (the gravel below sea - 10 comes from the
// surface rules), no vegetation.

#include "biome_defs.h"
#include "tree_shapes.h"

const BiomeDef OCEAN_DEF = {
    "Ocean",
    SAND, SAND, SAND,
    0.0f, nullptr,
    nullptr,
    nullptr,
    {1.0f, 1.0f, 1.0f}, // grass tint
    {1.0f, 1.0f, 1.0f}, // foliage tint
};
