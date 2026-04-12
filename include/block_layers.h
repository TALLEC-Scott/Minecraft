#pragma once

// Pure block→texture-layer mapping — GL-free so it's usable from the
// offline mesh builder (chunk_mesh.cpp) and from tests. TextureArray
// re-exports the same constants/functions for the rendering path.

#include "cube.h"

namespace block_layers {

constexpr int GRASS_SIDE_LAYER = 11;
constexpr int SUN_LAYER = 12;
constexpr int SNOW_LAYER = 13;
constexpr int GRAVEL_LAYER = 14;
constexpr int CACTUS_LAYER = 15;
constexpr int CLOUD_LAYER = 16;
constexpr int SKIN_LAYER = 17;
constexpr int MOON_LAYER = 18;
constexpr int WOOD_TOP_LAYER = 19;
constexpr int NUM_LAYERS = 20;

// Face-aware: grass uses different textures per face (top=grass,
// bottom=dirt, sides=grass_side). Wood is top/bottom vs sides. Other
// blocks with dedicated layers (snow, gravel, cactus) return theirs;
// everything else maps directly from block_type.
inline int layerForFace(block_type t, int face) {
    if (t == GRASS) {
        if (face == 4) return static_cast<int>(GRASS);
        if (face == 5) return static_cast<int>(DIRT);
        return GRASS_SIDE_LAYER;
    }
    if (t == WOOD) {
        if (face == 4 || face == 5) return WOOD_TOP_LAYER;
        return static_cast<int>(WOOD);
    }
    if (t == SNOW) return SNOW_LAYER;
    if (t == GRAVEL) return GRAVEL_LAYER;
    if (t == CACTUS) return CACTUS_LAYER;
    return static_cast<int>(t);
}

inline int layerForType(block_type t) { return static_cast<int>(t); }

} // namespace block_layers
