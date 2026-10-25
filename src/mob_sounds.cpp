#include "mob_sounds.h"

#include <fstream>
#include <string>

#include <miniaudio.h>

MobSoundBank::~MobSoundBank() {
    destroy();
}

const char* MobSoundBank::eventName(MobSoundEvent ev) {
    switch (ev) {
    case MobSoundEvent::Ambient:
        return "ambient";
    case MobSoundEvent::Hurt:
        return "hurt";
    case MobSoundEvent::Death:
        return "death";
    case MobSoundEvent::Step:
        return "step";
    case MobSoundEvent::Attack:
        return "attack";
    case MobSoundEvent::Count:
        break;
    }
    return "unknown";
}

MobSoundBank::Variants MobSoundBank::loadVariants(const std::string& prefix) {
    Variants out;
    if (!engine) return out;
    for (int n = 1; n <= MAX_VARIANTS; ++n) {
        std::string path = prefix + std::to_string(n) + ".ogg";
        // Cheap existence probe first so missing files don't spam
        // miniaudio's decoder error path.
        if (!std::ifstream(path).good()) continue;
        auto* s = new ma_sound{};
        if (ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, s) != MA_SUCCESS) {
            delete s;
            continue;
        }
        out.push_back(s);
        ++loadedCount;
    }
    return out;
}

void MobSoundBank::init(ma_engine* eng) {
    destroy();
    engine = eng;
    if (!engine) return;
    const MobInfo* table = mobRegistry();
    for (int t = 0; t < MOB_TYPE_COUNT; ++t) {
        for (int e = 0; e < static_cast<int>(MobSoundEvent::Count); ++e) {
            std::string prefix = std::string("assets/Sounds/mob/") + table[t].name + "/" +
                                 eventName(static_cast<MobSoundEvent>(e));
            sounds[t][e] = loadVariants(prefix);
        }
    }
}

void MobSoundBank::destroy() {
    auto freeAll = [](Variants& v) {
        for (ma_sound* s : v) {
            ma_sound_uninit(s);
            delete s;
        }
        v.clear();
    };
    for (auto& perType : sounds)
        for (auto& v : perType) freeAll(v);
    for (auto& v : extras) freeAll(v);
    extras.clear();
    loadedCount = 0;
}

int MobSoundBank::loadExtra(const char* pathPrefix) {
    Variants v = loadVariants(pathPrefix);
    if (v.empty()) return -1;
    extras.push_back(std::move(v));
    return static_cast<int>(extras.size()) - 1;
}

void MobSoundBank::playVariant(Variants& v, float volume) {
    if (v.empty() || volume <= 0.0f) return;
    ma_sound* s = v[std::uniform_int_distribution<size_t>(0, v.size() - 1)(rng)];
    ma_sound_set_volume(s, volume);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}

void MobSoundBank::play(MobType type, MobSoundEvent ev, glm::vec3 pos, glm::vec3 listener) {
    if (!engine || loadedCount == 0) return;
    int t = static_cast<int>(type), e = static_cast<int>(ev);
    if (t < 0 || t >= MOB_TYPE_COUNT || e < 0 || e >= static_cast<int>(MobSoundEvent::Count)) return;
    float d = glm::distance(pos, listener);
    float volume = 1.0f - d / HEARING_DISTANCE;
    playVariant(sounds[t][e], volume);
}

void MobSoundBank::playExtra(int key, float volume) {
    if (key < 0 || key >= static_cast<int>(extras.size())) return;
    playVariant(extras[key], volume);
}
