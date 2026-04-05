#include "texture_array.h"
#include <stb/stb_image.h>
#include <iostream>

GLuint TextureArray::id = 0;

// Must match block_type enum order: AIR, GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE
static const char* TEXTURE_PATHS[] = {
    "./Textures/none.png",
    "./Textures/grass.png",
    "./Textures/dirt.png",
    "./Textures/stone.png",
    "./Textures/coal_ore.png",
    "./Textures/bedrock.png",
    "./Textures/water.png",
    "./Textures/sand.png",
    "./Textures/glowstone.png",
    "./Textures/wood.png",
    "./Textures/leaves.png",
    "./Textures/grass_side.png",
    "./Textures/sun.png",
};
static constexpr int NUM_LAYERS = 13;

void TextureArray::initialize() {
    stbi_set_flip_vertically_on_load(true);

    int width = 16, height = 16;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, NUM_LAYERS,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        int w, h, channels;
        unsigned char* data = stbi_load(TEXTURE_PATHS[layer], &w, &h, &channels, 4);
        if (!data) {
            std::cerr << "TextureArray: failed to load " << TEXTURE_PATHS[layer] << std::endl;
            // Fill with magenta to signal missing texture
            unsigned char magenta[16 * 16 * 4];
            for (int i = 0; i < 16 * 16; i++) {
                magenta[i*4+0] = 255; magenta[i*4+1] = 0;
                magenta[i*4+2] = 255; magenta[i*4+3] = 255;
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, magenta);
        } else {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void TextureArray::bind() {
    glBindTexture(GL_TEXTURE_2D_ARRAY, id);
}

void TextureArray::destroy() {
    glDeleteTextures(1, &id);
    id = 0;
}
