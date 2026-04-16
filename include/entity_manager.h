#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "tnt_entity.h"

class World;
class Shader;
class EntityCubeRenderer;
struct ma_engine;
struct ma_sound;

// Owns and drives all non-player entities. Currently: primed TNT only.
// Ticks at 20 TPS via an accumulator so fuse length is FPS-independent.
class EntityManager {
  public:
    static constexpr double TICK_SECONDS = 0.05; // 20 TPS
    static constexpr int NUM_EXPLODE_SOUNDS = 4;

    ~EntityManager();

    void spawnTnt(glm::vec3 pos, int fuse, double now);

    // Advance time by `dt` seconds, running zero or more 20-TPS ticks.
    // Detonations call back into `world->explode(pos, power)` when a fuse
    // reaches zero. `now` is the wall-clock time used for visual-flash phase.
    void tick(World* world, float dt, double now);

    void render(const Shader& shader, glm::mat4 viewProjection, EntityCubeRenderer& cubes, double now);

    // Load explosion/fuse sounds. Safe to call if miniaudio isn't available.
    void initAudio(ma_engine* engine);
    // Play one of the N explode samples at `pos` (volume not yet spatialized).
    void playExplosion(glm::vec3 pos);

    size_t liveCount() const;

    std::vector<std::unique_ptr<TntEntity>>& tnts() { return tntEntities; }
    const std::vector<std::unique_ptr<TntEntity>>& tnts() const { return tntEntities; }

  private:
    std::vector<std::unique_ptr<TntEntity>> tntEntities;
    double accumulator = 0.0;
    ma_sound* explodeSounds[NUM_EXPLODE_SOUNDS] = {nullptr, nullptr, nullptr, nullptr};
    bool audioLoaded = false;
};
