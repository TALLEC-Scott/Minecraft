#pragma once

#include <array>
#include <glm/glm.hpp>

#include "gl_header.h"

class Shader;

// Lightweight pooled particle system — first of its kind in this codebase.
// One VBO that we rewrite per-frame with all live particle quads, drawn via
// the existing billboardShader. Used today for TNT explosion smoke plumes;
// the spawnSmokePlume() call is the entry point.
struct Particle {
    glm::vec3 pos;
    glm::vec3 velocity; // world units per second
    glm::vec3 tint = glm::vec3(1.0f);
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 1.0f;   // billboard half-extent in world units
    float growth = 0.5f; // added to size per second
    bool alive = false;
    bool buoyant = true; // smoke/bubbles rise; underwater motes should not
};

class ParticleSystem {
  public:
    static constexpr int POOL_SIZE = 512;

    ~ParticleSystem() { destroy(); }

    void init();
    void destroy();

    // Per-frame advance: decays life, integrates motion + drag, culls dead
    // entries. Safe to call when no particles are live.
    void update(float dt);

    // Renders all live particles as camera-facing quads through the
    // billboard shader. Caller is responsible for setting projection/view
    // on the shader beforehand.
    void render(const Shader& billboardShader, glm::vec3 cameraPos, glm::vec3 cameraFront);

    // Spawn ~40 white-smoke puffs centered on `pos` — Minecraft-style plume.
    void spawnSmokePlume(glm::vec3 pos);

    // Small white puffs that drift upward — used for the splash bubbles
    // when the player enters the water.
    void spawnBubbles(glm::vec3 pos, int count);

    // Tiny floating motes spawned around the camera while submerged. They
    // hold almost still in world space, so when the player moves the
    // parallax against them reads as water rushing past. `lightFactor` is a
    // 0..1 scene-brightness scalar (daylight × depth attenuation) baked into
    // the tint at spawn time.
    void spawnUnderwaterDrift(glm::vec3 cameraPos, glm::vec3 cameraFront, int count, float lightFactor);

    size_t aliveCount() const;

  private:
    std::array<Particle, POOL_SIZE> pool{};
    size_t nextIdx = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;
    bool glReady = false;

    Particle& allocParticle();
};
