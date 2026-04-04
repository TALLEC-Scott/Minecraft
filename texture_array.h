#pragma once
#include <glad/glad.h>
#include "cube.h"

class TextureArray {
public:
    static void initialize();
    static void bind();
    static void destroy();
    static int layerForType(block_type t) { return static_cast<int>(t); }
    static GLuint id;
};
