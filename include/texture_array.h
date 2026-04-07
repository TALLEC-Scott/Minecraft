#pragma once
#include "gl_header.h"
#include "cube.h"

class TextureArray {
  public:
    static void initialize();
    static void bind();
    static void destroy();
    static int layerForType(block_type t) { return static_cast<int>(t); }
    // Face-aware: grass uses different textures per face (top=grass, bottom=dirt, sides=grass_side)
    static int layerForFace(block_type t, int face) {
        if (t == GRASS) {
            if (face == 4) return static_cast<int>(GRASS);
            if (face == 5) return static_cast<int>(DIRT);
            return GRASS_SIDE_LAYER;
        }
        if (t == WOOD) {
            if (face == 4 || face == 5) return WOOD_TOP_LAYER; // top and bottom
            return static_cast<int>(WOOD);                     // sides = bark
        }
        if (t == SNOW) return SNOW_LAYER;
        if (t == GRAVEL) return GRAVEL_LAYER;
        if (t == CACTUS) return CACTUS_LAYER;
        return static_cast<int>(t);
    }
    static constexpr int GRASS_SIDE_LAYER = 11;
    static constexpr int SUN_LAYER = 12;
    static constexpr int SNOW_LAYER = 13;
    static constexpr int GRAVEL_LAYER = 14;
    static constexpr int CACTUS_LAYER = 15;
    static constexpr int CLOUD_LAYER = 16;
    static constexpr int SKIN_LAYER = 17;
    static constexpr int MOON_LAYER = 18;
    static constexpr int WOOD_TOP_LAYER = 19;
    static constexpr int NUM_LAYERS = 20;
    static void initLayerTextures();
    static GLuint getLayerTexture2D(int layer);
    static void destroyLayerTextures();
    static GLuint id;
};
