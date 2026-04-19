#pragma once

#include <glm/glm.hpp>

#include "gl_header.h"

class Shader;

// Renders a Steve-shaped humanoid at a given world position. Builds a
// single unit-cube VAO once (same attribute layout as EntityCubeRenderer
// / the chunk shader) and issues 6 scaled/translated draws per player —
// head, torso, two arms, two legs — each tinted via the chunk shader's
// entityColor uniform. MVP stand-in for a proper skinned-mesh player.
class PlayerRenderer {
  public:
    ~PlayerRenderer();

    // Must be called with a current GL context.
    void init();

    // `feetPos` is the world position of the player's feet (not the
    // camera). `yaw` rotates the whole body around Y so it faces the
    // peer's look direction. `pitch` tilts the head only.
    void draw(const Shader& shader, glm::vec3 feetPos, float yaw, float pitch);

    void destroy();

  private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    bool glReady = false;
};
