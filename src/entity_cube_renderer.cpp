#include "entity_cube_renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "cube.h"
#include "shader.h"
#include "texture_array.h"

namespace {

// Vertex layout matches the chunk shader (assets/Shaders/vert.shd) exactly:
//   loc 0: pos (3f)
//   loc 1: uv  (2f)
//   loc 2: normalIdx (1f, face 0..5 → NORMALS[int(idx)] in the shader)
//   loc 3: texLayer  (1f)
//   loc 4: aAO       (1f, low 2 bits index AO_CURVE — we pack 3 for full bright)
//   loc 5: aPackedLight (1f, sky*16 + block — 240 = full sky, no block)
// The chunk shader multiplies aPos by 0.5, so we emit ±1.0 corners and the
// cube renders at half-size 0.5 (i.e. a 1×1×1 block) after translation by
// the `model` uniform.
struct Face {
    float v[4][3];
    float u[4][2];
    int faceIdx; // same index fed to the shader and to block_layers::layerForFace
};

const Face FACES[6] = {
    {{{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 0},     // +Z front
    {{{1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 1}, // -Z back
    {{{-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}, {-1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 2}, // -X left
    {{{1, -1, 1}, {1, 1, 1}, {1, 1, -1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 3},     // +X right
    {{{-1, 1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 4},     // +Y top
    {{{-1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 5}, // -Y bottom
};

constexpr int VERT_FLOATS = 9;
constexpr int NUM_VERTS = 24;
constexpr int NUM_INDICES = 36;

} // namespace

EntityCubeRenderer::~EntityCubeRenderer() {
    destroy();
}

void EntityCubeRenderer::init() {
    if (glReady) return;
    float verts[NUM_VERTS * VERT_FLOATS];
    unsigned int indices[NUM_INDICES];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        float layer = static_cast<float>(TextureArray::layerForFace(TNT, FACES[f].faceIdx));
        float normalIdx = static_cast<float>(FACES[f].faceIdx);
        for (int v = 0; v < 4; ++v) {
            verts[vi++] = FACES[f].v[v][0];
            verts[vi++] = FACES[f].v[v][1];
            verts[vi++] = FACES[f].v[v][2];
            verts[vi++] = FACES[f].u[v][0];
            verts[vi++] = FACES[f].u[v][1];
            verts[vi++] = normalIdx;
            verts[vi++] = layer;
            // AO byte: low 2 bits = AO_CURVE index (3 → full bright).
            verts[vi++] = 3.0f;
            // Full skylight (15<<4 | 0 = 240); sun/moon contribution still
            // scales by time of day.
            verts[vi++] = 240.0f;
        }
        int b = f * 4;
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
    // loc 2: aNormalIdx — single float face index (NOT a vec3). The chunk
    // shader declares `in float aNormalIdx` and indexes a 6-entry NORMALS
    // table; passing a 3-float vector would send only nx and mis-index the
    // table (and trip undefined behaviour on the -X face).
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(5);
    glBindVertexArray(0);
    glReady = true;
}

void EntityCubeRenderer::drawTnt(const Shader& shader, glm::mat4 /*vp*/, glm::vec3 worldPos, float flashBrightness) {
    if (!glReady) init();
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPos);
    shader.setMat4("model", model);
    shader.setFloat("entityTint", flashBrightness);
    shader.setInt("materialType", 0);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    // Reset tint so subsequent non-entity draws keep normal brightness.
    shader.setFloat("entityTint", 1.0f);
}

void EntityCubeRenderer::destroy() {
    if (!glReady) return;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    glReady = false;
}
