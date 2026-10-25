#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "item_entity.h"
#include "tnt_entity.h"

class World;
class Shader;
class EntityCubeRenderer;
struct ma_engine;
struct ma_sound;

// Owns and drives all non-player entities: primed TNT and dropped items.
// Ticks at 20 TPS via an accumulator so fuse length is FPS-independent.
class EntityManager {
  public:
    static constexpr double TICK_SECONDS = 0.05; // 20 TPS
    static constexpr int NUM_EXPLODE_SOUNDS = 4;
    static constexpr size_t MAX_ITEMS = 512; // oldest drops vanish past this

    ~EntityManager();

    void spawnTnt(glm::vec3 pos, int fuse, double now);

    // Drops `count` of `id` with its bottom at `pos`, moving at `velocity`
    // (blocks per tick). `pickupDelay` ticks pass before anyone can take it.
    void spawnItem(ItemId id, int count, glm::vec3 pos, glm::vec3 velocity,
                   int pickupDelay = ItemEntity::PICKUP_DELAY);
    // A block's drop: from the block centre with Minecraft's small random pop.
    void dropFromBlock(ItemId id, int count, glm::ivec3 block);
    // Thrown from `eyePos`: tossed along `dir` (a dying player's spill uses
    // a random direction per stack).
    void throwItem(ItemId id, int count, glm::vec3 eyePos, glm::vec3 dir);

    // Called each tick for every item that can be picked up; returns how
    // many the taker accepted (0 = not in reach / no room). Set by main.cpp
    // to the local player.
    using PickupHandler = std::function<int(const ItemEntity&)>;
    void setPickupHandler(PickupHandler handler) { pickup = std::move(handler); }
    const std::vector<ItemEntity>& items() const { return itemEntities; }
    float partialTick() const { return static_cast<float>(accumulator / TICK_SECONDS); }

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
    std::vector<ItemEntity> itemEntities;
    PickupHandler pickup;
    std::mt19937 rng{std::random_device{}()};
    double accumulator = 0.0;
    void tickItems(World* world);
    ma_sound* explodeSounds[NUM_EXPLODE_SOUNDS] = {nullptr, nullptr, nullptr, nullptr};
    bool audioLoaded = false;
};
