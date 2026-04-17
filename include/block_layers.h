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
// TNT is block_type value 14, so its "side" layer = 20 (same as the enum index once we append).
// But block_type AIR=0...CACTUS=13, TNT=14. Layers 0..10 match block_type 0..10; 11..19 are
// auxiliary (grass_side, sun, etc). We want TNT side at layer 20, plus top/bottom.
constexpr int TNT_SIDE_LAYER = 20;
constexpr int TNT_TOP_LAYER = 21;
constexpr int TNT_BOTTOM_LAYER = 22;
constexpr int SMOKE_LAYER = 23;
constexpr int TALL_GRASS_LAYER = 24;
constexpr int DANDELION_LAYER = 25;
constexpr int POPPY_LAYER = 26;
constexpr int NUM_LAYERS = 27;

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
    if (t == TNT) {
        if (face == 4) return TNT_TOP_LAYER;
        if (face == 5) return TNT_BOTTOM_LAYER;
        return TNT_SIDE_LAYER;
    }
    if (t == TALL_GRASS) return TALL_GRASS_LAYER;
    if (t == DANDELION) return DANDELION_LAYER;
    if (t == POPPY) return POPPY_LAYER;
    return static_cast<int>(t);
}

inline int layerForType(block_type t) {
    if (t == TNT) return TNT_SIDE_LAYER;
    if (t == TALL_GRASS) return TALL_GRASS_LAYER;
    if (t == DANDELION) return DANDELION_LAYER;
    if (t == POPPY) return POPPY_LAYER;
    return static_cast<int>(t);
}

} // namespace block_layers
