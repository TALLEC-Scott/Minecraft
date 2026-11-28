#include "torch_model.h"

#include <vector>

#include "block_layers.h"
#include "shader.h"
#include "torch.h"

namespace {

// Same faces, corner order and winding as EntityCubeRenderer, on a ±1 cube.
struct Face {
    float v[4][3];
    float n[3];
    int faceIdx; // chunk shader NORMALS index: 0 +Z, 1 -Z, 2 -X, 3 +X, 4 +Y, 5 -Y
};
const Face FACES[6] = {
    {{{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}}, {0, 0, 1}, 0},
    {{{1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}}, {0, 0, -1}, 1},
    {{{-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}, {-1, -1, 1}}, {-1, 0, 0}, 2},
    {{{1, -1, 1}, {1, 1, 1}, {1, 1, -1}, {1, -1, -1}}, {1, 0, 0}, 3},
    {{{-1, 1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}}, {0, 1, 0}, 4},
    {{{-1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}}, {0, -1, 0}, 5},
};
const float CORNER_UV[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
constexpr float PX = 1.0f / 16.0f;

// One box of the model: its height range and its texture rows (texture-array
// UVs load flipped, so v = 0 is the bottom row). The stick is columns 7-8.
struct Part {
    float y0, y1;  // model-space height
    float v0, v1;  // side texture rows
    float capV0;   // top / bottom face rows (2 px)
    int skipFace;  // the face hidden inside the other part
};
// Body: rows 8-15 (wood), bottom face = the stick's end. Tip: rows 6-7
// (the flame), top face = the flame seen from above.
constexpr float TIP_V0 = 10 * PX - torch::TIP_HEIGHT;
const Part PARTS[2] = {
    {0.0f, torch::STICK_HEIGHT - torch::TIP_HEIGHT, 0 * PX, TIP_V0, 0 * PX, 4},
    {torch::STICK_HEIGHT - torch::TIP_HEIGHT, torch::STICK_HEIGHT, TIP_V0, 10 * PX, 8 * PX, 5},
};
constexpr int FACES_PER_PART = 5;
constexpr int INDICES_PER_PART = FACES_PER_PART * 6;

constexpr int WORLD_FLOATS = 9; // chunk shader: pos (×2), uv, normalIdx, layer, ao, packedLight
constexpr int VIEW_FLOATS = 10; // billboard shader: pos, uv, normal, layer, brightness

} // namespace

TorchModel::~TorchModel() {
    destroy();
}

void TorchModel::init() {
    if (glReady) return;
    std::vector<float> worldVerts, viewVerts;
    std::vector<unsigned int> indices;
    const auto layer = static_cast<float>(block_layers::TORCH_LAYER);
    unsigned int base = 0;
    for (const Part& part : PARTS) {
        for (const Face& f : FACES) {
            if (f.faceIdx == part.skipFace) continue;
            bool cap = f.faceIdx >= 4;
            float v0 = cap ? part.capV0 : part.v0, v1 = cap ? part.capV0 + 2 * PX : part.v1;
            for (int c = 0; c < 4; ++c) {
                glm::vec3 p(f.v[c][0] * torch::STICK_HALF, f.v[c][1] < 0 ? part.y0 : part.y1,
                            f.v[c][2] * torch::STICK_HALF);
                float u = 7 * PX + CORNER_UV[c][0] * 2 * PX;
                float v = v0 + CORNER_UV[c][1] * (v1 - v0);
                // The chunk shader halves positions (its int16 ×2 encoding).
                const float world[WORLD_FLOATS] = {
                    p.x * 2.0f, p.y * 2.0f, p.z * 2.0f, u, v, static_cast<float>(f.faceIdx), layer,
                    3.0f /* AO_CURVE[3]: no occlusion */, 0.0f /* light: set per draw */};
                // The tip's top face is marked by its brightness (billboard
                // glowMode 3 gives it its own colour; the rings come from v).
                bool topFace = &part == &PARTS[1] && f.faceIdx == 4;
                const float view[VIEW_FLOATS] = {
                    p.x, p.y, p.z, u, v, f.n[0], f.n[1], f.n[2], layer, topFace ? torch::TIP_TOP_FACE_BOOST : 1.0f};
                worldVerts.insert(worldVerts.end(), world, world + WORLD_FLOATS);
                viewVerts.insert(viewVerts.end(), view, view + VIEW_FLOATS);
            }
            for (unsigned int k : {0u, 1u, 2u, 2u, 3u, 0u}) indices.push_back(base + k);
            base += 4;
        }
    }

    glGenBuffers(1, &ebo);
    glGenVertexArrays(1, &worldVao);
    glGenBuffers(1, &worldVbo);
    glBindVertexArray(worldVao);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(worldVerts.size() * sizeof(float)), worldVerts.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);
    const int worldSizes[6] = {3, 2, 1, 1, 1, 1};
    for (int a = 0, off = 0; a < 6; off += worldSizes[a], ++a) {
        glVertexAttribPointer(a, worldSizes[a], GL_FLOAT, GL_FALSE, WORLD_FLOATS * sizeof(float),
                              (void*)(off * sizeof(float)));
        glEnableVertexAttribArray(a);
    }
    glDisableVertexAttribArray(5); // packed light comes from glVertexAttrib1f per draw

    glGenVertexArrays(1, &viewVao);
    glGenBuffers(1, &viewVbo);
    glBindVertexArray(viewVao);
    glBindBuffer(GL_ARRAY_BUFFER, viewVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(viewVerts.size() * sizeof(float)), viewVerts.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    const int viewSizes[5] = {3, 2, 3, 1, 1};
    for (int a = 0, off = 0; a < 5; off += viewSizes[a], ++a) {
        glVertexAttribPointer(a, viewSizes[a], GL_FLOAT, GL_FALSE, VIEW_FLOATS * sizeof(float),
                              (void*)(off * sizeof(float)));
        glEnableVertexAttribArray(a);
    }
    glBindVertexArray(0);
    glReady = true;
}

void TorchModel::drawWorld(const Shader& chunkShader, const glm::mat4& model, float packedLight,
                           const torch::TipColors& tipColor) {
    init();
    chunkShader.setMat4("model", model);
    chunkShader.setFloat("entityTint", 1.0f);
    chunkShader.setInt("materialType", 0);
    glBindVertexArray(worldVao);
    glVertexAttrib1f(5, packedLight);
    glDrawElements(GL_TRIANGLES, INDICES_PER_PART, GL_UNSIGNED_INT, nullptr);
    // The flame cap: flat glowing rings + top face (frag.shd `glowColor`
    // upper ring, `glowColorLow` lower, `glowColorFace` top), lit by
    // nothing, so it shines the same day and night.
    chunkShader.setVec4("glowColor", glm::vec4(tipColor.top, 1.0f));
    chunkShader.setVec3("glowColorLow", tipColor.bottom);
    chunkShader.setVec3("glowColorFace", tipColor.face);
    glDrawElements(GL_TRIANGLES, INDICES_PER_PART, GL_UNSIGNED_INT,
                   (void*)(INDICES_PER_PART * sizeof(unsigned int)));
    chunkShader.setVec4("glowColor", glm::vec4(0.0f));
    glBindVertexArray(0);
}

void TorchModel::drawView(const Shader& billboardShader, const glm::mat4& model, glm::vec3 bodyTint,
                          const torch::TipColors& tipColor) {
    init();
    billboardShader.setMat4("model", model);
    billboardShader.setVec3("tintColor", bodyTint);
    glBindVertexArray(viewVao);
    glDrawElements(GL_TRIANGLES, INDICES_PER_PART, GL_UNSIGNED_INT, nullptr);
    billboardShader.setVec3("tintColor", tipColor.top);
    billboardShader.setVec3("tintColorLow", tipColor.bottom);
    billboardShader.setVec3("tintColorFace", tipColor.face);
    billboardShader.setFloat("glowMode", 3.0f); // torch tip rings
    glDrawElements(GL_TRIANGLES, INDICES_PER_PART, GL_UNSIGNED_INT,
                   (void*)(INDICES_PER_PART * sizeof(unsigned int)));
    billboardShader.setFloat("glowMode", 0.0f);
    glBindVertexArray(0);
}

void TorchModel::destroy() {
    if (!glReady) return;
    glDeleteVertexArrays(1, &worldVao);
    glDeleteVertexArrays(1, &viewVao);
    glDeleteBuffers(1, &worldVbo);
    glDeleteBuffers(1, &viewVbo);
    glDeleteBuffers(1, &ebo);
    worldVao = viewVao = worldVbo = viewVbo = ebo = 0;
    glReady = false;
}
