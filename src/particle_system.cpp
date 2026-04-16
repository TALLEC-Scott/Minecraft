#include "particle_system.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "block_layers.h"
#include "shader.h"

namespace {

std::mt19937& rng() {
    static std::mt19937 g(4242u);
    return g;
}

float rand01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng());
}

// Same 10-float billboard vertex layout used by sun/moon: pos(3) + uv(2) +
// normal(3) + layer(1) + brightness(1). We upload N particles × 4 verts/frame
// and draw with an index buffer of 6 indices per quad.
constexpr int VERT_FLOATS = 10;
constexpr int VERTS_PER_QUAD = 4;
constexpr int INDICES_PER_QUAD = 6;

} // namespace

void ParticleSystem::init() {
    if (glReady) return;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // Pre-size VBO for max pool; we glBufferSubData each frame for live quads.
    glBufferData(GL_ARRAY_BUFFER, POOL_SIZE * VERTS_PER_QUAD * VERT_FLOATS * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Index buffer never changes — build it once.
    std::vector<unsigned int> indices;
    indices.reserve(POOL_SIZE * INDICES_PER_QUAD);
    for (int i = 0; i < POOL_SIZE; ++i) {
        int b = i * VERTS_PER_QUAD;
        indices.push_back(b);
        indices.push_back(b + 1);
        indices.push_back(b + 2);
        indices.push_back(b + 2);
        indices.push_back(b + 3);
        indices.push_back(b);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    constexpr int STRIDE = VERT_FLOATS * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
    glReady = true;
}

void ParticleSystem::destroy() {
    if (!glReady) return;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    glReady = false;
}

Particle& ParticleSystem::allocParticle() {
    // Simple round-robin — overwrites oldest if pool is saturated. With
    // POOL_SIZE=512 and a 40-particle plume, you'd need ~13 simultaneous
    // explosions to clip particles, which is fine for gameplay.
    Particle& p = pool[nextIdx];
    nextIdx = (nextIdx + 1) % POOL_SIZE;
    return p;
}

void ParticleSystem::update(float dt) {
    for (auto& p : pool) {
        if (!p.alive) continue;
        p.life += dt;
        if (p.life >= p.maxLife) {
            p.alive = false;
            continue;
        }
        // Buoyancy: smoke accelerates upward slightly; horizontal drag so it
        // spreads then settles. Tuned by eye to look like a Minecraft plume.
        p.velocity.y += 0.8f * dt;
        float drag = std::exp(-1.2f * dt);
        p.velocity.x *= drag;
        p.velocity.z *= drag;
        p.pos += p.velocity * dt;
        p.size += p.growth * dt;
    }
}

void ParticleSystem::render(const Shader& billboardShader, glm::vec3 cameraPos, glm::vec3 cameraFront) {
    if (!glReady) init();

    // Build up to POOL_SIZE quads. Compute right/up axes per-particle so they
    // always face the camera regardless of head angle.
    std::vector<float> verts;
    verts.reserve(POOL_SIZE * VERTS_PER_QUAD * VERT_FLOATS);

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    int liveQuads = 0;
    for (auto& p : pool) {
        if (!p.alive) continue;
        glm::vec3 toCam = glm::normalize(cameraPos - p.pos);
        glm::vec3 right = glm::cross(worldUp, toCam);
        if (glm::dot(right, right) < 1e-5f) right = glm::vec3(1, 0, 0);
        right = glm::normalize(right) * p.size;
        glm::vec3 up = glm::normalize(glm::cross(toCam, right / p.size)) * p.size;
        (void)cameraFront;

        // Alpha fade: strong in middle, fades edges. Packed into the
        // brightness attribute — billboard_frag multiplies both RGB and
        // alpha by it, so we get a smooth disappearance.
        float t = p.life / p.maxLife;
        float alpha = (1.0f - t) * (1.0f - t);
        // Cap brightness at 0.85 so multiple particles don't blow out to
        // pure white when they stack; still reads as "thick smoke".
        float bright = 0.85f * alpha;

        float layer = static_cast<float>(block_layers::SMOKE_LAYER);
        glm::vec3 v0 = p.pos - right - up;
        glm::vec3 v1 = p.pos - right + up;
        glm::vec3 v2 = p.pos + right + up;
        glm::vec3 v3 = p.pos + right - up;

        auto emit = [&](glm::vec3 pos, float u, float v) {
            verts.push_back(pos.x);
            verts.push_back(pos.y);
            verts.push_back(pos.z);
            verts.push_back(u);
            verts.push_back(v);
            verts.push_back(0.0f); // normal ignored by billboard_frag
            verts.push_back(0.0f);
            verts.push_back(1.0f);
            verts.push_back(layer);
            verts.push_back(bright);
        };
        emit(v0, 0, 0);
        emit(v1, 0, 1);
        emit(v2, 1, 1);
        emit(v3, 1, 0);
        ++liveQuads;
    }

    if (liveQuads == 0) return;

    billboardShader.setMat4("model", glm::mat4(1.0f));
    billboardShader.setVec3("tintColor", glm::vec3(1.0f));
    billboardShader.setFloat("glowMode", 0.0f);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
    glDrawElements(GL_TRIANGLES, liveQuads * INDICES_PER_QUAD, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ParticleSystem::spawnSmokePlume(glm::vec3 pos) {
    constexpr int COUNT = 40;
    for (int i = 0; i < COUNT; ++i) {
        Particle& p = allocParticle();
        float angle = rand01() * 6.2831853f;
        float horizSpeed = 0.3f + rand01() * 0.9f;
        float upSpeed = 1.2f + rand01() * 1.8f;
        // Jitter spawn position slightly so the plume has volume.
        p.pos = pos + glm::vec3((rand01() - 0.5f) * 1.5f, (rand01() - 0.2f) * 0.8f, (rand01() - 0.5f) * 1.5f);
        p.velocity = glm::vec3(std::cos(angle) * horizSpeed, upSpeed, std::sin(angle) * horizSpeed);
        p.life = 0.0f;
        p.maxLife = 1.6f + rand01() * 0.9f;
        p.size = 0.7f + rand01() * 0.5f;
        p.growth = 0.8f + rand01() * 0.4f;
        p.alive = true;
    }
}

size_t ParticleSystem::aliveCount() const {
    size_t n = 0;
    for (const auto& p : pool)
        if (p.alive) ++n;
    return n;
}
