#pragma once
#include <chrono>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include "cube.h"

class World;
class Chunk;
struct ma_sound;
struct ma_engine;

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        size_t seed = std::hash<int>()(v.x);
        hashCombine(seed, v.y);
        hashCombine(seed, v.z);
        return seed;
    }
};

// Per-cell state captured once in the pre-pass and consumed by the main
// pass. Folding it here lets the main pass skip the chunk lookup + water
// byte read it would otherwise redo for every active cell.
struct WaterTickCell {
    glm::ivec3 pos;
    Chunk* chunk;
    int lx;
    int lz;
    uint8_t raw;
    bool willDecay;
};

class WaterSimulator {
  public:
    explicit WaterSimulator(World* world) : world(world) {}
    ~WaterSimulator();
    void tick();
    void updateAmbient(glm::vec3 playerPos);
    void initAudio(ma_engine* engine);
    void activate(int x, int y, int z);
    void activateNeighbors(int x, int y, int z);

    static constexpr int MAX_BLOCKS_PER_TICK = 512;
    // Seconds between ticks — FPS-agnostic, wall-clock timed.
    // 0.25s = 4 ticks/second (matches Minecraft's fluid tick rate).
    static constexpr double TICK_SECONDS = 0.25;
    // Force the next tick() call to fire (used by tests).
    bool forceNextTick = false;
    std::chrono::steady_clock::time_point lastTickTime;

  private:
    World* world;
    std::unordered_set<glm::ivec3, IVec3Hash> activeBlocks;
    std::unordered_set<glm::ivec3, IVec3Hash> nextActive;
    // Scratch buffer reused each tick — pre-pass fills it, main pass
    // drains it. Cleared via .clear() to keep the backing storage.
    std::vector<WaterTickCell> tickCells;
    // Heap-allocated so WaterSimulator.h doesn't pull <miniaudio.h>
    // into every translation unit that sees a World.
    // CA transition sound (plays during active block changes)
    ma_sound* flowSound = nullptr;
    // Ambient loop for proximity to flowing water (plays when player is near)
    ma_sound* ambientFlowSound = nullptr;
    float ambientVolume = 0.0f; // smoothed volume for fade in/out
    bool audioLoaded = false;
};
