#pragma once

// Per-mob sound effects. Missing files degrade to a silent no-op. Sounds
// live as OGG Vorbis (Git LFS, like every *.ogg in the repo) at
//     assets/Sounds/mob/<name>/<event>N.ogg
// where <name> is the registry name (e.g. "pig"), <event> is one of
// ambient | hurt | death | step | attack, and N = 1..MAX_VARIANTS. The
// bank picks a random present variant per play. (Web builds only see
// files bundled by --preload-file, i.e. anything under assets/.)

#include <array>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "mob_context.h"
#include "mob_registry.h"

struct ma_engine;
struct ma_sound;

class MobSoundBank {
  public:
    static constexpr int MAX_VARIANTS = 4;
    static constexpr float HEARING_DISTANCE = 16.0f; // linear falloff to silence

    MobSoundBank() = default;
    ~MobSoundBank();
    MobSoundBank(const MobSoundBank&) = delete;
    MobSoundBank& operator=(const MobSoundBank&) = delete;

    // Probe + decode every registered mob's sounds. Safe with a null engine.
    void init(ma_engine* engine);
    void destroy();

    // Play `ev` for `type` at `pos`, attenuated by distance to `listener`.
    void play(MobType type, MobSoundEvent ev, glm::vec3 pos, glm::vec3 listener);
    // Play a one-off file (e.g. the player's hurt sound) if it was found by
    // loadExtra(). `key` is the index returned by loadExtra; -1 is a no-op.
    int loadExtra(const char* pathPrefix); // tries <prefix>1.ogg..<prefix>4.ogg
    void playExtra(int key, float volume = 1.0f);

    static const char* eventName(MobSoundEvent ev);
    bool hasAny() const { return loadedCount > 0; }

  private:
    using Variants = std::vector<ma_sound*>;
    std::array<std::array<Variants, static_cast<size_t>(MobSoundEvent::Count)>, MOB_TYPE_COUNT> sounds{};
    std::vector<Variants> extras;
    ma_engine* engine = nullptr;
    int loadedCount = 0;
    std::mt19937 rng{0x50D5u};

    Variants loadVariants(const std::string& prefix);
    void playVariant(Variants& v, float volume);
};
