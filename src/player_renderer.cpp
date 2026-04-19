#include "player_renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "shader.h"
#include "texture_array.h"

namespace {

// Vertex layout mirrors EntityCubeRenderer + the chunk shader:
//   vec3 pos, vec2 uv, float normalIdx, float texLayer, float ao, float packedLight
// The cube's corners are at ±1; the chunk shader halves them (aPos*0.5)
// so the drawn cube spans [-0.5, 0.5] — i.e., a 1×1×1 block at the origin.
// All body-part sizing happens via the model matrix at draw time.
struct Face {
    float v[4][3];
    float u[4][2];
    int   faceIdx;
};

const Face FACES[6] = {
    {{{-1, -1, 1}, {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 0},
    {{{1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 1},
    {{{-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}, {-1, -1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 2},
    {{{1, -1, 1}, {1, 1, 1}, {1, 1, -1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 3},
    {{{-1, 1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 4},
    {{{-1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 5},
};

constexpr int VERT_FLOATS = 9;
constexpr int NUM_VERTS = 24;
constexpr int NUM_INDICES = 36;

// Standard Minecraft Steve proportions (16 pixels = 1 block). Feet at y=0,
// head top at y=2.0. Eye (≈ top-minus-a-bit) ≈ y=1.62 which matches the
// camera's PLAYER_HEIGHT constant — so drawing at feetPos lines up.
//
// Head-attached parts (eyes, mouth) rotate around the head's center so
// they track the peer's pitch without drifting relative to the face. The
// body, arms, and legs rotate with yaw only.
struct BodyPart {
    glm::vec3 center;   // offset from feet (body parts) or from head center (head-attached)
    glm::vec3 halfSize; // half-extents
    glm::vec3 color;    // entityColor tint
    bool headAttached;  // true → pitches around HEAD_CENTER
};

const glm::vec3 HEAD_CENTER(0.0f, 1.75f, 0.0f);

const glm::vec3 SKIN_TONE(0.95f, 0.78f, 0.63f);
const glm::vec3 SHIRT(0.12f, 0.65f, 0.78f); // cyan, like vanilla Steve
const glm::vec3 PANTS(0.22f, 0.23f, 0.55f); // denim-ish
const glm::vec3 SHOES(0.25f, 0.18f, 0.10f); // dark boots
const glm::vec3 EYE_WHITE(0.96f, 0.96f, 0.96f);
const glm::vec3 EYE_PUPIL(0.08f, 0.08f, 0.16f); // near-black with a touch of blue
const glm::vec3 MOUTH(0.30f, 0.15f, 0.12f);     // darker reddish-brown

// Head half-extent is 0.25. Face details sit at z = +0.26 (just proud of
// the front face so they don't z-fight) in head-local coords.
constexpr float FACE_Z = 0.26f;

const BodyPart PARTS[] = {
    // --- Head ---
    // Base head cube. Pitches around HEAD_CENTER; its own center is the
    // pivot so the geometry is identical to any other body part but it
    // gets the pitch rotation applied via the head-attached path.
    {{0.0f, 0.0f, 0.0f}, {0.25f, 0.25f, 0.25f}, SKIN_TONE, true},
    // Eyes: small white squares, pupils inset in front. Head-local coords
    // are relative to HEAD_CENTER so the face stays glued to the head as
    // it pitches.
    {{-0.09f, 0.06f, FACE_Z}, {0.06f, 0.04f, 0.005f}, EYE_WHITE, true},
    {{0.09f, 0.06f, FACE_Z}, {0.06f, 0.04f, 0.005f}, EYE_WHITE, true},
    {{-0.09f, 0.06f, FACE_Z + 0.006f}, {0.03f, 0.03f, 0.005f}, EYE_PUPIL, true},
    {{0.09f, 0.06f, FACE_Z + 0.006f}, {0.03f, 0.03f, 0.005f}, EYE_PUPIL, true},
    // Mouth: thin horizontal slab.
    {{0.0f, -0.08f, FACE_Z}, {0.07f, 0.015f, 0.005f}, MOUTH, true},

    // --- Torso (t-shirt) ---
    {{0.0f, 1.125f, 0.0f}, {0.25f, 0.375f, 0.125f}, SHIRT, false},

    // --- Arms: short cyan sleeve at the shoulder, skin below (t-shirt
    //     look). Left arm is at -X; right at +X when the character faces
    //     +Z. Each original arm spanned y∈[0.75,1.5]; split into sleeve
    //     (top 0.1875) + exposed skin (bottom 0.5625). ---
    {{-0.375f, 1.40625f, 0.0f}, {0.1275f, 0.09375f, 0.1275f}, SHIRT, false},
    {{-0.375f, 1.03125f, 0.0f}, {0.125f, 0.28125f, 0.125f}, SKIN_TONE, false},
    {{0.375f, 1.40625f, 0.0f}, {0.1275f, 0.09375f, 0.1275f}, SHIRT, false},
    {{0.375f, 1.03125f, 0.0f}, {0.125f, 0.28125f, 0.125f}, SKIN_TONE, false},

    // --- Legs ---
    {{-0.125f, 0.375f, 0.0f}, {0.125f, 0.375f, 0.125f}, PANTS, false},
    {{0.125f, 0.375f, 0.0f}, {0.125f, 0.375f, 0.125f}, PANTS, false},

    // --- Boots ---
    {{-0.125f, 0.03f, 0.0f}, {0.13f, 0.03f, 0.13f}, SHOES, false},
    {{0.125f, 0.03f, 0.0f}, {0.13f, 0.03f, 0.13f}, SHOES, false},
};
constexpr int NUM_PARTS = sizeof(PARTS) / sizeof(PARTS[0]);

} // namespace

PlayerRenderer::~PlayerRenderer() { destroy(); }

void PlayerRenderer::init() {
    if (glReady) return;
    float verts[NUM_VERTS * VERT_FLOATS];
    unsigned int indices[NUM_INDICES];
    int vi = 0;
    float layer = static_cast<float>(TextureArray::SKIN_LAYER);
    for (int f = 0; f < 6; ++f) {
        float normalIdx = static_cast<float>(FACES[f].faceIdx);
        for (int v = 0; v < 4; ++v) {
            verts[vi++] = FACES[f].v[v][0];
            verts[vi++] = FACES[f].v[v][1];
            verts[vi++] = FACES[f].v[v][2];
            verts[vi++] = FACES[f].u[v][0];
            verts[vi++] = FACES[f].u[v][1];
            verts[vi++] = normalIdx;
            verts[vi++] = layer;
            verts[vi++] = 3.0f;   // AO=3 (full bright)
            verts[vi++] = 240.0f; // full skylight, no block light
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

void PlayerRenderer::draw(const Shader& shader, glm::vec3 feetPos, float yaw, float pitch) {
    if (!glReady) init();

    // The session's yaw comes straight from Player::getYaw(), which uses the
    // same convention as the camera (OpenGL standard: yaw measured from +X
    // toward +Z, so yaw=0 looks along +X). Our cube's front face is +Z, so
    // rotate by (yaw + 90°) around Y to align.
    float bodyYawRad = glm::radians(-yaw - 90.0f);
    glm::mat4 base = glm::translate(glm::mat4(1.0f), feetPos) *
                     glm::rotate(glm::mat4(1.0f), bodyYawRad, glm::vec3(0, 1, 0));

    shader.setInt("materialType", 0);
    shader.setFloat("entityTint", 1.0f);
    glBindVertexArray(vao);

    // Pitch is clamped so the head can't spin past the shoulders.
    float pitchRad = glm::radians(-glm::clamp(pitch, -70.0f, 70.0f));
    glm::mat4 headFrame = base * glm::translate(glm::mat4(1.0f), HEAD_CENTER) *
                          glm::rotate(glm::mat4(1.0f), pitchRad, glm::vec3(1, 0, 0));

    for (int i = 0; i < NUM_PARTS; ++i) {
        const BodyPart& p = PARTS[i];
        glm::mat4 m;
        if (p.headAttached) {
            // Head-local offset is relative to HEAD_CENTER; inherits the
            // head's pitch so the face stays glued to the skull.
            m = headFrame * glm::translate(glm::mat4(1.0f), p.center);
        } else {
            m = base * glm::translate(glm::mat4(1.0f), p.center);
        }
        // Cube vertex positions span ±1 but the chunk shader halves them.
        // Scaling by halfSize*2 lands the cube at exactly ±halfSize.
        m = m * glm::scale(glm::mat4(1.0f), p.halfSize * 2.0f);
        shader.setMat4("model", m);
        shader.setVec3("entityColor", p.color);
        glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_INT, nullptr);
    }

    // Restore defaults so later chunk/entity draws aren't tinted.
    shader.setVec3("entityColor", glm::vec3(1.0f));
    shader.setMat4("model", glm::mat4(1.0f));
    glBindVertexArray(0);
}

void PlayerRenderer::destroy() {
    if (!glReady) return;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    glReady = false;
}
