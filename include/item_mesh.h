#pragma once

// Minecraft-style held-item meshes: a 16×16 item sprite extruded one pixel
// deep. Every opaque pixel becomes a 1×1×1 cell; front and back faces are
// always emitted, side faces only where the neighboring pixel is
// transparent (or off the image), so the slab shows a solid pixel edge.
// GL-free for tests; held_item_model.h uploads and draws the result.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct ItemMeshVertex {
    glm::vec3 pos; // pixel units: x right, y up (image row 0 at the top), z in [-0.5, 0.5]
    glm::vec3 normal;
    glm::vec3 color; // 0..1
};

// `rgba` is width*height*4 bytes, top row first (stb_image without flip).
// Pixels with alpha < 128 are empty. Returns 3 vertices per triangle.
std::vector<ItemMeshVertex> extrudeSprite(const uint8_t* rgba, int width, int height);
