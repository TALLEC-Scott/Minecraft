#include "particle_system.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <glm/gtc/epsilon.hpp>
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
        // Cartoonish buoyancy: a mild upward lift plus heavy drag so puffs
        // float rather than fly. Minecraft smoke reads as slow drifting
        // clumps, not fast streamers. Inert motes skip buoyancy so they
        // hang in the water column.
        if (p.buoyant) p.velocity.y += 0.25f * dt;
        float drag = std::exp(p.buoyant ? -2.5f * dt : -0.6f * dt);
        p.velocity *= drag;
        p.pos += p.velocity * dt;
        // Growth tapers off: most expansion happens in the first 40% of
        // the life, then the puff holds size as it fades.
        float lifeT = p.life / p.maxLife;
        float growthCurve = (lifeT < 0.4f) ? (1.0f - lifeT / 0.4f) : 0.0f;
        p.size += p.growth * growthCurve * dt;
    }
}

void ParticleSystem::render(const Shader& billboardShader, glm::vec3 cameraPos, glm::vec3 cameraFront) {
    if (!glReady) init();
    (void)cameraFront;

    // Bucket particles by unique tint so we can tint-per-batch via the
    // existing tintColor uniform without adding a per-vertex attribute.
    // The common case (all white) compacts into one pass.
    struct Batch {
        glm::vec3 tint;
        std::vector<float> verts;
        int quads = 0;
    };
    std::vector<Batch> batches;
    auto findBatch = [&](glm::vec3 t) -> Batch& {
        for (auto& b : batches) {
            if (glm::all(glm::epsilonEqual(b.tint, t, 0.001f))) return b;
        }
        batches.push_back({t, {}, 0});
        batches.back().verts.reserve(64 * VERTS_PER_QUAD * VERT_FLOATS);
        return batches.back();
    };

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    for (auto& p : pool) {
        if (!p.alive) continue;
        glm::vec3 toCam = glm::normalize(cameraPos - p.pos);
        glm::vec3 right = glm::cross(worldUp, toCam);
        if (glm::dot(right, right) < 1e-5f) right = glm::vec3(1, 0, 0);
        right = glm::normalize(right) * p.size;
        glm::vec3 up = glm::normalize(glm::cross(toCam, right / p.size)) * p.size;

        // Cartoonish alpha curve: fully opaque for ~1/3 of life, then a
        // steep linear fade. Reads as a solid puff that suddenly
        // disappears rather than smoothly dissolving.
        float t = p.life / p.maxLife;
        float alpha = (t < 0.33f) ? 1.0f : std::max(0.0f, 1.5f - 1.5f * t);
        float bright = alpha;

        float layer = static_cast<float>(block_layers::SMOKE_LAYER);
        glm::vec3 v0 = p.pos - right - up;
        glm::vec3 v1 = p.pos - right + up;
        glm::vec3 v2 = p.pos + right + up;
        glm::vec3 v3 = p.pos + right - up;

        Batch& b = findBatch(p.tint);
        auto emit = [&](glm::vec3 pos, float u, float v) {
            b.verts.push_back(pos.x);
            b.verts.push_back(pos.y);
            b.verts.push_back(pos.z);
            b.verts.push_back(u);
            b.verts.push_back(v);
            b.verts.push_back(0.0f);
            b.verts.push_back(0.0f);
            b.verts.push_back(1.0f);
            b.verts.push_back(layer);
            b.verts.push_back(bright);
        };
        emit(v0, 0, 0);
        emit(v1, 0, 1);
        emit(v2, 1, 1);
        emit(v3, 1, 0);
        ++b.quads;
    }

    if (batches.empty()) return;

    billboardShader.setMat4("model", glm::mat4(1.0f));
    billboardShader.setFloat("glowMode", 0.0f);

    glBindVertexArray(vao);
    for (auto& b : batches) {
        if (b.quads == 0) continue;
        billboardShader.setVec3("tintColor", b.tint);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, b.verts.size() * sizeof(float), b.verts.data());
        glDrawElements(GL_TRIANGLES, b.quads * INDICES_PER_QUAD, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void ParticleSystem::spawnSmokePlume(glm::vec3 pos) {
    // Fewer, bigger, slower puffs — reads as cartoony Minecraft smoke
    // clouds rather than a fast realistic plume.
    constexpr int COUNT = 14;
    for (int i = 0; i < COUNT; ++i) {
        Particle& p = allocParticle();
        float angle = rand01() * 6.2831853f;
        float horizSpeed = 0.4f + rand01() * 0.6f;
        float upSpeed = 0.6f + rand01() * 0.8f;
        p.pos = pos + glm::vec3((rand01() - 0.5f) * 2.0f, (rand01() - 0.3f) * 1.2f, (rand01() - 0.5f) * 2.0f);
        p.velocity = glm::vec3(std::cos(angle) * horizSpeed, upSpeed, std::sin(angle) * horizSpeed);
        p.life = 0.0f;
        p.maxLife = 1.8f + rand01() * 0.8f;
        p.size = 1.4f + rand01() * 0.7f;   // chunky from the start
        p.growth = 2.5f + rand01() * 1.0f; // pops bigger quickly, then holds
        p.alive = true;
    }
}

void ParticleSystem::spawnBubbles(glm::vec3 pos, int count) {
    for (int i = 0; i < count; ++i) {
        Particle& p = allocParticle();
        float angle = rand01() * 6.2831853f;
        float r = rand01() * 0.6f;
        p.pos = pos + glm::vec3(std::cos(angle) * r,
                                (rand01() - 0.3f) * 0.6f,
                                std::sin(angle) * r);
        p.velocity = glm::vec3((rand01() - 0.5f) * 0.8f,
                               2.2f + rand01() * 1.2f,
                               (rand01() - 0.5f) * 0.8f);
        p.tint = glm::vec3(0.55f, 0.80f, 1.00f); // cool water-bubble blue
        p.life = 0.0f;
        p.maxLife = 0.5f + rand01() * 0.4f;
        p.size = 0.08f + rand01() * 0.06f;
        p.growth = 0.12f;
        p.alive = true;
    }
}

void ParticleSystem::spawnUnderwaterDrift(glm::vec3 cameraPos, glm::vec3 cameraFront, int count, float lightFactor) {
    // Spawn points sit in a sphere mostly in front of the camera so the
    // player sees the motes drift through their view rather than behind
    // their head. Particles hold almost still — parallax from player motion
    // does the visual work. Tiny sizes + lighting-scaled tint keep them subtle.
    float lf = std::clamp(lightFactor, 0.0f, 1.0f);
    glm::vec3 base(0.82f, 0.84f, 0.86f); // grayish-white, hair cooler than neutral
    for (int i = 0; i < count; ++i) {
        Particle& p = allocParticle();
        glm::vec3 biased = cameraFront * (1.5f + rand01() * 2.5f);
        glm::vec3 jitter((rand01() - 0.5f) * 5.0f,
                         (rand01() - 0.5f) * 3.0f,
                         (rand01() - 0.5f) * 5.0f);
        p.pos = cameraPos + biased + jitter;
        p.velocity = glm::vec3((rand01() - 0.5f) * 0.06f,
                               (rand01() - 0.3f) * 0.04f,
                               (rand01() - 0.5f) * 0.06f);
        // Light-scaled gray. Floor ensures motes don't vanish completely at
        // night — a tiny spectral glow is still readable.
        p.tint = base * (0.15f + 0.85f * lf);
        p.life = 0.0f;
        p.maxLife = 4.0f + rand01() * 3.0f;
        p.size = 0.012f + rand01() * 0.015f;
        p.growth = 0.0f;
        p.buoyant = false;
        p.alive = true;
    }
}

size_t ParticleSystem::aliveCount() const {
    size_t n = 0;
    for (const auto& p : pool)
        if (p.alive) ++n;
    return n;
}
