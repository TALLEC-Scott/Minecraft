#pragma once

// Lava's sounds and particles, after Minecraft's LavaFluid.animateTick and
// the lava-extinguish event:
//   - 20 times a second, 667 random blocks within 16 of the listener are
//     checked; a lava block with air above pops (1 in 100: lavapop.ogg + an
//     ember) and bubbles (1 in 200: lava.ogg).
//   - lava that hardens against water (WaterSimulator::fizzes) or an item
//     burning up hisses (fizz.ogg, high pitched) under a puff of smoke.
// Volumes fall off linearly to silence at HEARING_DISTANCE. Missing sound
// files are skipped silently (web builds load sounds late).

#include <array>
#include <random>

#include <glm/glm.hpp>

class World;
struct ma_engine;
struct ma_sound;

class LavaEffects {
  public:
    static constexpr int SAMPLES_PER_TICK = 667; // Minecraft's animateTick count
    static constexpr int SAMPLE_RADIUS = 16;
    static constexpr int POP_CHANCE = 100;     // 1 in N per sampled surface lava block
    static constexpr int AMBIENT_CHANCE = 200;
    static constexpr float HEARING_DISTANCE = 16.0f;
    static constexpr int MAX_FIZZES_PER_TICK = 4; // a big pour hardening at once stays one hiss, not 50

    LavaEffects() = default;
    ~LavaEffects();
    LavaEffects(const LavaEffects&) = delete;
    LavaEffects& operator=(const LavaEffects&) = delete;

    void initAudio(ma_engine* engine);
    // Drains world.waterSimulator->fizzes and runs the 20 TPS ambient pass.
    void update(const World& world, glm::vec3 listener, float dt);
    // Hiss + smoke at `pos` (world position), e.g. an item burning in lava.
    // Heard from world.lastCameraPos.
    void fizz(const World& world, glm::vec3 pos);

  private:
    static constexpr int VOICES = 3; // overlapping plays per sound
    using Voices = std::array<ma_sound*, VOICES>;
    Voices ambient{}, pop{}, hiss{};
    std::array<int, 3> nextVoice{};
    bool audioLoaded = false;
    float accumulator = 0.0f;
    int fizzesThisTick = 0;
    std::mt19937 rng{0x1A7Au};

    void loadVoices(ma_engine* engine, const char* path, Voices& v);
    void play(Voices& v, int& next, float volume, float pitch);
    float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
};
