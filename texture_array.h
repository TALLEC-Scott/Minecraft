#pragma once
#include <glad/glad.h>
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
        if (t == SNOW) return SNOW_LAYER;
        if (t == GRAVEL) return GRAVEL_LAYER;
        return static_cast<int>(t);
    }
    static constexpr int GRASS_SIDE_LAYER = 11;
    static constexpr int SUN_LAYER = 12;
    static constexpr int SNOW_LAYER = 13;
    static constexpr int GRAVEL_LAYER = 14;
    static GLuint id;
};
