#pragma once

// Draws one held item in first person: its sprite extruded into a pixel
// slab (item_mesh.h), shaded by assets/Shaders/item_*.shd with vertex
// colors, a fixed key light, and the arm's light tint.

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "gl_header.h"

class Shader;

// How a held item is lit. Defaults suit first person (view space, no fog).
struct ItemShading {
    glm::vec3 lightTint{1.0f};          // sun/moon/sky/block light at the item
    glm::vec3 keyDir{0.3f, 0.8f, 0.5f}; // key light, same space as the model output
    glm::vec3 cameraPos{0.0f};          // fog origin, same space
    glm::vec3 fogColor{0.0f};
    float fogStart = 1e9f, fogEnd = 2e9f; // far away: no fog
};

class HeldItemModel {
  public:
    HeldItemModel() = default;
    ~HeldItemModel();
    HeldItemModel(const HeldItemModel&) = delete;
    HeldItemModel& operator=(const HeldItemModel&) = delete;

    // Builds the mesh from the sprite PNG + loads the shader on the first
    // call; later calls are no-ops. False (draws nothing) if it failed.
    bool load(const std::string& spritePath);
    // `projection` × `model` must reach clip space: view-space matrices in
    // first person, viewProjection + world model in third.
    void draw(const glm::mat4& projection, const glm::mat4& model, const ItemShading& shading) const;

  private:
    bool tried_ = false;
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei vertexCount_ = 0;
    std::unique_ptr<Shader> shader_;
};
