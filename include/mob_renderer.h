#pragma once

// Draws data-driven MobModels with the chunk shader. One shared unit-cube
// VAO (same vertex layout as EntityCubeRenderer / PlayerRenderer) is
// scaled + posed per part. Texture layer and packed light are NOT vertex
// arrays in this VAO: attributes 3 (texLayer) and 5 (packedLight) are left
// disabled and fed per draw through glVertexAttrib1f, so each part can pick
// its own texture-array layer and each mob is lit by the light at its own
// position (dark in caves, bright at noon) without rebuilding buffers.

#include <glm/glm.hpp>

#include "gl_header.h"
#include "mob_model.h"

class Shader;
class Mob;

class MobRenderer {
  public:
    ~MobRenderer();

    // Builds the cube VAO. Called lazily by the draw functions; requires a
    // current GL context.
    void init();
    void destroy();

    // Draw `mob` using its model() + pose(partialTick): interpolated
    // position/yaw, red hurt tint, death tip-over. `packedLight` is
    // sky * 16 + block (0-255) sampled at the mob.
    void drawMob(const Shader& shader, const Mob& mob, float partialTick, float packedLight);

    // Low-level: draw any model under `root` (feet-origin, +Z forward).
    void drawModel(const Shader& shader, const MobModel& model, const MobPose& pose, const glm::mat4& root,
                   glm::vec3 tint, float packedLight);

    // One box (unit cube scaled by `boxMatrix`), e.g. projectiles.
    void drawBox(const Shader& shader, const glm::mat4& boxMatrix, glm::vec3 color, float packedLight,
                 int textureLayer = -1);

  private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    bool glReady = false;
};
