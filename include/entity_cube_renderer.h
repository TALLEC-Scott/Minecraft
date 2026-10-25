#pragma once

#include <glm/glm.hpp>

#include <array>

#include "cube.h"
#include "gl_header.h"

class Shader;

// Renders a unit cube (1×1×1) at arbitrary world positions using the
// existing chunk shader. Shared by all entities that look like a block —
// today just primed TNT. The cube mesh is built once; the per-entity draw
// only re-uploads the `model` uniform and a `entityTint` brightness scalar.
class EntityCubeRenderer {
  public:
    ~EntityCubeRenderer();

    // Builds the cube VAO/VBO. Must be called with a current GL context.
    // Faces are uploaded with placeholder texture layers; drawTnt() rewrites
    // the layer attribute just-in-time from the TNT block's per-face layers.
    void init();

    // Draw one primed-TNT cube. `flashBrightness` multiplies the final fragment
    // colour (1.0 = normal, >1.0 = whitened flash).
    void drawTnt(const Shader& shader, glm::mat4 viewProjection, glm::vec3 worldPos, float flashBrightness);

    // Draw a block of `type` with its per-face textures (held blocks on
    // remote players). `model` places a 1×1×1 cube; scale it down to shrink.
    // Meshes are built per type on first use.
    void drawBlock(const Shader& shader, const glm::mat4& model, block_type type);

    void destroy();

  private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    bool glReady = false;

    struct BlockMesh {
        GLuint vao = 0, vbo = 0, ebo = 0;
    };
    std::array<BlockMesh, static_cast<size_t>(POPPY) + 1> blockMeshes{};

    static void buildCube(block_type type, GLuint& vao, GLuint& vbo, GLuint& ebo);
};
