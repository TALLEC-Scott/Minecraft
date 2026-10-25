#include "held_item_model.h"

#include <cstddef>
#include <iostream>
#include <vector>

#include <stb/stb_image.h>

#include "item_mesh.h"
#include "shader.h"

HeldItemModel::~HeldItemModel() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool HeldItemModel::load(const std::string& spritePath) {
    if (tried_) return vertexCount_ > 0;
    tried_ = true;
    // Top row first, as extrudeSprite expects; restore the global flip the
    // texture-array loader relies on.
    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(spritePath.c_str(), &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(true);
    if (!data) {
        std::cerr << "HeldItemModel: failed to load " << spritePath << std::endl;
        return false;
    }
    std::vector<ItemMeshVertex> verts = extrudeSprite(data, w, h);
    stbi_image_free(data);
    if (verts.empty()) return false;
    shader_ = std::make_unique<Shader>("assets/Shaders/item_vert.shd", "assets/Shaders/item_frag.shd");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(ItemMeshVertex)), verts.data(),
                 GL_STATIC_DRAW);
    constexpr GLsizei STRIDE = sizeof(ItemMeshVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)offsetof(ItemMeshVertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)offsetof(ItemMeshVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)offsetof(ItemMeshVertex, color));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    vertexCount_ = static_cast<GLsizei>(verts.size());
    return true;
}

void HeldItemModel::draw(const glm::mat4& projection, const glm::mat4& model, const ItemShading& shading) const {
    if (vertexCount_ == 0) return;
    shader_->use();
    shader_->setMat4("projection", projection);
    shader_->setMat4("model", model);
    shader_->setVec3("lightTint", shading.lightTint);
    shader_->setVec3("keyDir", shading.keyDir);
    shader_->setVec3("cameraPos", shading.cameraPos);
    shader_->setVec3("fogColor", shading.fogColor);
    shader_->setFloat("fogStart", shading.fogStart);
    shader_->setFloat("fogEnd", shading.fogEnd);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glBindVertexArray(0);
}
