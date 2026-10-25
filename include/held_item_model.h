#pragma once

// Draws one held item in first person: its sprite extruded into a pixel
// slab (item_mesh.h), shaded by assets/Shaders/item_*.shd with vertex
// colors, a fixed key light, and the arm's light tint.

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "gl_header.h"

class Shader;

class HeldItemModel {
  public:
    HeldItemModel() = default;
    ~HeldItemModel();
    HeldItemModel(const HeldItemModel&) = delete;
    HeldItemModel& operator=(const HeldItemModel&) = delete;

    // Builds the mesh from the sprite PNG + loads the shader on the first
    // call; later calls are no-ops. False (draws nothing) if it failed.
    bool load(const std::string& spritePath);
    // `lightTint` is the first-person arm light (sun/moon/sky at the camera).
    void draw(const glm::mat4& projection, const glm::mat4& model, glm::vec3 lightTint) const;

  private:
    bool tried_ = false;
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei vertexCount_ = 0;
    std::unique_ptr<Shader> shader_;
};
