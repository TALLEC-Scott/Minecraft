#include "mob_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "mob.h"
#include "shader.h"
#include "texture_array.h"

namespace {

// Same cube as EntityCubeRenderer: corners at ±1, halved by the chunk
// shader (aPos * 0.5) into a unit cube centered on the origin.
struct Face {
    float v[4][3];
    float u[4][2];
    int faceIdx;
};

const Face FACES[6] = {
    {{{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 0},     // +Z front
    {{{1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 1}, // -Z back
    {{{-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}, {-1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 2}, // -X left
    {{{1, -1, 1}, {1, 1, 1}, {1, 1, -1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 3},     // +X right
    {{{-1, 1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 4},     // +Y top
    {{{-1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 5}, // -Y bottom
};

// pos(3) uv(2) normalIdx(1) ao(1). texLayer (loc 3) and packedLight
// (loc 5) come from glVertexAttrib1f constants per draw.
constexpr int VERT_FLOATS = 7;
constexpr int NUM_VERTS = 24;
constexpr int NUM_INDICES = 36;
constexpr GLuint LOC_TEX_LAYER = 3;
constexpr GLuint LOC_PACKED_LIGHT = 5;

const glm::vec3 HURT_TINT(1.0f, 0.45f, 0.45f);

float layerFor(int textureLayer) {
    return static_cast<float>(textureLayer >= 0 ? textureLayer : TextureArray::SKIN_LAYER);
}

} // namespace

MobRenderer::~MobRenderer() {
    destroy();
}

void MobRenderer::init() {
    if (glReady) return;
    float verts[NUM_VERTS * VERT_FLOATS];
    unsigned int indices[NUM_INDICES];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            verts[vi++] = FACES[f].v[v][0];
            verts[vi++] = FACES[f].v[v][1];
            verts[vi++] = FACES[f].v[v][2];
            verts[vi++] = FACES[f].u[v][0];
            verts[vi++] = FACES[f].u[v][1];
            verts[vi++] = static_cast<float>(FACES[f].faceIdx);
            verts[vi++] = 3.0f; // AO index 3 = no occlusion
        }
        unsigned int b = static_cast<unsigned int>(f) * 4;
        indices[f * 6 + 0] = b;
        indices[f * 6 + 1] = b + 1;
        indices[f * 6 + 2] = b + 2;
        indices[f * 6 + 3] = b + 2;
        indices[f * 6 + 4] = b + 3;
        indices[f * 6 + 5] = b;
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    constexpr int STRIDE = VERT_FLOATS * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // Locations 3 and 5 intentionally stay disabled → constant attributes.
    glDisableVertexAttribArray(LOC_TEX_LAYER);
    glDisableVertexAttribArray(LOC_PACKED_LIGHT);
    glBindVertexArray(0);
    glReady = true;
}

void MobRenderer::destroy() {
    if (!glReady) return;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    glReady = false;
}

void MobRenderer::drawModel(const Shader& shader, const MobModel& model, const MobPose& pose, const glm::mat4& root,
                            glm::vec3 tint, float packedLight) {
    if (!glReady) init();
    int n = std::min(model.size(), MAX_MOB_PARTS);
    std::array<glm::mat4, MAX_MOB_PARTS> frames;

    shader.setInt("materialType", 0);
    shader.setFloat("entityTint", 1.0f + std::clamp(pose.whiteFlash, 0.0f, 1.0f));
    glBindVertexArray(vao);
    glVertexAttrib1f(LOC_PACKED_LIGHT, packedLight);
    // Emissive parts keep the mob's sky light (so they still follow the
    // day/night tint) but force block light to 15.
    float emissiveLight = std::floor(packedLight / 16.0f) * 16.0f + 15.0f;
    bool lastEmissive = false;

    for (int i = 0; i < n; ++i) {
        const MobPart& p = model.parts[i];
        // Parents must precede children (MobModel::add order guarantees it
        // for well-formed models); anything else attaches to the root.
        const glm::mat4& parent = (p.parent >= 0 && p.parent < i) ? frames[p.parent] : root;
        glm::vec3 r = p.restRotation + pose.rotation[i];
        glm::mat4 m = glm::translate(parent, p.pivot + pose.offset[i]);
        if (r.z != 0.0f) m = glm::rotate(m, r.z, glm::vec3(0, 0, 1));
        if (r.y != 0.0f) m = glm::rotate(m, r.y, glm::vec3(0, 1, 0));
        if (r.x != 0.0f) m = glm::rotate(m, r.x, glm::vec3(1, 0, 0));
        frames[i] = m;
        if (pose.hidden[i]) continue;

        glm::mat4 box = glm::scale(glm::translate(m, p.center), p.halfSize * 2.0f);
        shader.setMat4("model", box);
        shader.setVec3("entityColor", p.color * tint);
        glVertexAttrib1f(LOC_TEX_LAYER, layerFor(p.textureLayer));
        if (p.emissive != lastEmissive) {
            glVertexAttrib1f(LOC_PACKED_LIGHT, p.emissive ? emissiveLight : packedLight);
            lastEmissive = p.emissive;
        }
        glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_INT, nullptr);
    }

    shader.setVec3("entityColor", glm::vec3(1.0f));
    shader.setFloat("entityTint", 1.0f);
    shader.setMat4("model", glm::mat4(1.0f));
    glBindVertexArray(0);
}

void MobRenderer::drawMob(const Shader& shader, const Mob& mob, float partialTick, float packedLight) {
    MobPose pose = mob.pose(partialTick);
    glm::mat4 root = glm::translate(glm::mat4(1.0f), mob.renderPos(partialTick));
    // Model faces +Z; yaw 0 faces +X. Rotating +Z by (90° - yaw) about Y
    // lands it on (cos yaw, 0, sin yaw).
    root = glm::rotate(root, glm::radians(90.0f - mob.renderYaw(partialTick)), glm::vec3(0, 1, 0));
    float death = mob.deathProgress(partialTick);
    if (death > 0.0f) root = glm::rotate(root, death * glm::half_pi<float>(), glm::vec3(0, 0, 1));
    glm::mat4 flameRoot = root; // hitbox-sized, so not the model's scale
    root = glm::scale(root, pose.scale);
    glm::vec3 tint = (mob.hurtTicks > 0 || mob.dying) ? HURT_TINT : glm::vec3(1.0f);
    drawModel(shader, mob.model(), pose, root, tint, packedLight);

    if (!mob.burning()) return;
    // Flames keep the mob's sky light but glow at block light 15.
    float emissiveLight = std::floor(packedLight / 16.0f) * 16.0f + 15.0f;
    float twist = (static_cast<float>(mob.ageTicks) + partialTick) * 0.24f;
    for (const Mob::FlameBox& f : mob.flameBoxes(partialTick)) {
        glm::mat4 m = glm::rotate(glm::translate(flameRoot, f.center), twist, glm::vec3(0, 1, 0));
        drawBox(shader, glm::scale(m, f.halfSize * 2.0f), f.color, emissiveLight);
    }
}

void MobRenderer::drawBox(const Shader& shader, const glm::mat4& boxMatrix, glm::vec3 color, float packedLight,
                          int textureLayer) {
    if (!glReady) init();
    shader.setInt("materialType", 0);
    shader.setFloat("entityTint", 1.0f);
    shader.setMat4("model", boxMatrix);
    shader.setVec3("entityColor", color);
    glBindVertexArray(vao);
    glVertexAttrib1f(LOC_PACKED_LIGHT, packedLight);
    glVertexAttrib1f(LOC_TEX_LAYER, layerFor(textureLayer));
    glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_INT, nullptr);
    shader.setVec3("entityColor", glm::vec3(1.0f));
    shader.setMat4("model", glm::mat4(1.0f));
    glBindVertexArray(0);
}
