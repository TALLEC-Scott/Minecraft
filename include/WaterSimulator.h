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

// How one liquid flows (Minecraft's values). Water and lava share the
// cellular automaton and differ only in these numbers.
struct FluidRules {
    block_type block;
    int levelStep;        // level added per block of spread (lava 2 → 3 blocks)
    int maxLevel;         // last level that exists (spreads while level + step <= this)
    bool infiniteSources; // 2+ source neighbours make a new source (water only)
    int dropScanRange;    // how far spread looks for a hole to pour into
    double tickSeconds;   // wall-clock time between ticks
};
inline constexpr FluidRules WATER_RULES{WATER, 1, WATER_MAX_FLOW, true, 4, 0.25};
inline constexpr FluidRules LAVA_RULES{LAVA, 2, 6, false, 2, 1.5};

// Water and lava cellular automata. Each liquid keeps its own active set and
// tick timer; lava also reacts with water it touches (Minecraft: lava
// source → obsidian, flowing lava → cobblestone, lava falling onto water →
// stone). The class keeps its old name — everything that changes blocks
// already wakes it through activate()/activateNeighbors().
class WaterSimulator {
  public:
    explicit WaterSimulator(World* world) : world(world) {}
    ~WaterSimulator();
    void tick();
    void updateAmbient(glm::vec3 playerPos);
    void initAudio(ma_engine* engine);
    // Wake a cell (and with activateNeighbors its 6 neighbours) for both
    // liquids; each liquid ignores cells that aren't its own.
    void activate(int x, int y, int z);
    void activateNeighbors(int x, int y, int z);

    static constexpr int MAX_BLOCKS_PER_TICK = 512;
    // Water's horizontal drop-seeking range (kept for tests and docs; the
    // per-liquid value lives in FluidRules).
    static constexpr int DROP_SCAN_RANGE = WATER_RULES.dropScanRange;
    // Water's tick interval: 4 ticks/second, Minecraft's fluid tick rate.
    static constexpr double TICK_SECONDS = WATER_RULES.tickSeconds;
    // Force the next tick() call to fire for both liquids (used by tests).
    bool forceNextTick = false;
    std::chrono::steady_clock::time_point lastTickTime; // water's (lava keeps its own)

  private:
    struct Channel {
        const FluidRules* rules;
        std::unordered_set<glm::ivec3, IVec3Hash> activeBlocks;
        std::unordered_set<glm::ivec3, IVec3Hash> nextActive;
        // Scratch buffer reused each tick — pre-pass fills it, main pass
        // drains it. Cleared via .clear() to keep the backing storage.
        std::vector<WaterTickCell> tickCells;
    };

    World* world;
    Channel water{&WATER_RULES, {}, {}, {}};
    Channel lava{&LAVA_RULES, {}, {}, {}};
    std::chrono::steady_clock::time_point lavaLastTickTime;

    // True when the channel's interval has elapsed (or a tick is forced).
    bool due(std::chrono::steady_clock::time_point& last, double interval) const;
    // One automaton step for `ch`; returns whether any block changed.
    bool tickChannel(Channel& ch);
    // Lava touching water hardens; returns true if the cell turned solid.
    bool hardenLava(const WaterTickCell& cell);
    // A water change can harden lava next to it: wake the lava around it.
    void wakeLavaAround(int x, int y, int z);

    // Heap-allocated so WaterSimulator.h doesn't pull <miniaudio.h>
    // into every translation unit that sees a World.
    // CA transition sound (plays during active block changes)
    ma_sound* flowSound = nullptr;
    // Ambient loop for proximity to flowing water (plays when player is near)
    ma_sound* ambientFlowSound = nullptr;
    float ambientVolume = 0.0f; // smoothed volume for fade in/out
    bool audioLoaded = false;
};
