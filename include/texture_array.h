#pragma once
#include "gl_header.h"
#include "cube.h"
#include "block_layers.h"

class TextureArray {
  public:
    static void initialize();
    static void bind();
    static void destroy();
    static int layerForType(block_type t) { return block_layers::layerForType(t); }
    static int layerForFace(block_type t, int face) { return block_layers::layerForFace(t, face); }
    static constexpr int GRASS_SIDE_LAYER = block_layers::GRASS_SIDE_LAYER;
    static constexpr int SUN_LAYER = block_layers::SUN_LAYER;
    static constexpr int SNOW_LAYER = block_layers::SNOW_LAYER;
    static constexpr int GRAVEL_LAYER = block_layers::GRAVEL_LAYER;
    static constexpr int CACTUS_LAYER = block_layers::CACTUS_LAYER;
    static constexpr int CLOUD_LAYER = block_layers::CLOUD_LAYER;
    static constexpr int SKIN_LAYER = block_layers::SKIN_LAYER;
    static constexpr int MOON_LAYER = block_layers::MOON_LAYER;
    static constexpr int WOOD_TOP_LAYER = block_layers::WOOD_TOP_LAYER;
    static constexpr int TNT_SIDE_LAYER = block_layers::TNT_SIDE_LAYER;
    static constexpr int TNT_TOP_LAYER = block_layers::TNT_TOP_LAYER;
    static constexpr int TNT_BOTTOM_LAYER = block_layers::TNT_BOTTOM_LAYER;
    static constexpr int SMOKE_LAYER = block_layers::SMOKE_LAYER;
    static constexpr int NUM_LAYERS = block_layers::NUM_LAYERS;
    static void initLayerTextures();
    static GLuint getLayerTexture2D(int layer);
    static void destroyLayerTextures();
    static GLuint id;
};
