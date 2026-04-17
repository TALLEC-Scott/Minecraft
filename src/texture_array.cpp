#include "texture_array.h"
#include <stb/stb_image.h>
#include <iostream>

GLuint TextureArray::id = 0;

// Must match block_type enum order: AIR, GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE
static const char* TEXTURE_PATHS[] = {
    "assets/Textures/none.png",     "assets/Textures/grass.png",       "assets/Textures/dirt.png",
    "assets/Textures/stone.png",    "assets/Textures/coal_ore.png",    "assets/Textures/bedrock.png",
    "assets/Textures/water.png",    "assets/Textures/sand.png",        "assets/Textures/glowstone.png",
    "assets/Textures/wood.png",     "assets/Textures/leaves.png",      "assets/Textures/grass_side.png",
    "assets/Textures/sun.png",      "assets/Textures/snow.png",        "assets/Textures/gravel.png",
    "assets/Textures/cactus.png",   "assets/Textures/cloud.png",       "assets/Textures/skin.png",
    "assets/Textures/moon.png",     "assets/Textures/wood_top.png",    "assets/Textures/tnt.png",
    "assets/Textures/tnt_top.png",  "assets/Textures/tnt_bottom.png",  "assets/Textures/smoke.png",
    "assets/Textures/tall_grass.png", "assets/Textures/dandelion.png", "assets/Textures/poppy.png",
};
static constexpr int NUM_LAYERS = TextureArray::NUM_LAYERS;
static GLuint layerTextures2D[NUM_LAYERS] = {};

void TextureArray::initialize() {
    stbi_set_flip_vertically_on_load(true);

    int width = 16, height = 16;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, NUM_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        int w, h, channels;
        unsigned char* data = stbi_load(TEXTURE_PATHS[layer], &w, &h, &channels, 4);
        if (!data) {
            std::cerr << "TextureArray: failed to load " << TEXTURE_PATHS[layer] << std::endl;
            // Fill with magenta to signal missing texture
            unsigned char magenta[16 * 16 * 4];
            for (int i = 0; i < 16 * 16; i++) {
                magenta[i * 4 + 0] = 255;
                magenta[i * 4 + 1] = 0;
                magenta[i * 4 + 2] = 255;
                magenta[i * 4 + 3] = 255;
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, magenta);
        } else {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
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

void TextureArray::initLayerTextures() {
    stbi_set_flip_vertically_on_load(false); // UI textures: top-left origin
    for (int i = 0; i < NUM_LAYERS; i++) {
        int w, h, channels;
        unsigned char* data = stbi_load(TEXTURE_PATHS[i], &w, &h, &channels, 4);
        if (!data) continue;
        glGenTextures(1, &layerTextures2D[i]);
        glBindTexture(GL_TEXTURE_2D, layerTextures2D[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        stbi_image_free(data);
    }
    stbi_set_flip_vertically_on_load(true); // restore for GL textures
}

GLuint TextureArray::getLayerTexture2D(int layer) {
    if (layer < 0 || layer >= NUM_LAYERS) return 0;
    return layerTextures2D[layer];
}

void TextureArray::destroyLayerTextures() {
    for (int i = 0; i < NUM_LAYERS; i++) {
        if (layerTextures2D[i]) {
            glDeleteTextures(1, &layerTextures2D[i]);
            layerTextures2D[i] = 0;
        }
    }
}
