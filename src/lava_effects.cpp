#include "lava_effects.h"

#include <miniaudio.h>

#include "particle_system.h"
#include "world.h"

LavaEffects::~LavaEffects() {
    for (Voices* v : {&ambient, &pop, &hiss})
        for (ma_sound*& s : *v) {
            if (!s) continue;
            ma_sound_uninit(s);
            delete s;
            s = nullptr;
        }
}

void LavaEffects::loadVoices(ma_engine* engine, const char* path, Voices& v) {
    for (int i = 0; i < VOICES; ++i) {
        auto* s = new ma_sound{};
        // Later voices share the first one's decoded data.
        ma_result r = i == 0 ? ma_sound_init_from_file(engine, path, MA_SOUND_FLAG_DECODE, nullptr, nullptr, s)
                             : ma_sound_init_copy(engine, v[0], MA_SOUND_FLAG_DECODE, nullptr, s);
        if (r != MA_SUCCESS) {
            delete s;
            return;
        }
        v[i] = s;
    }
}

void LavaEffects::initAudio(ma_engine* engine) {
    if (!engine || audioLoaded) return;
    loadVoices(engine, "assets/Sounds/liquid/lava.ogg", ambient);
    loadVoices(engine, "assets/Sounds/liquid/lavapop.ogg", pop);
    loadVoices(engine, "assets/Sounds/random/fizz.ogg", hiss);
    audioLoaded = true;
}

void LavaEffects::play(Voices& v, int& next, float volume, float pitch) {
    if (!audioLoaded || volume <= 0.0f) return;
    ma_sound* s = v[next];
    next = (next + 1) % VOICES;
    if (!s) return;
    ma_sound_set_volume(s, volume);
    ma_sound_set_pitch(s, pitch);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}

void LavaEffects::fizz(const World& world, glm::vec3 pos) {
    if (fizzesThisTick >= MAX_FIZZES_PER_TICK) return;
    ++fizzesThisTick;
    world.particles->spawnFizzSmoke(pos);
    // Minecraft: volume 0.5, pitch 2.6 ± 0.8.
    float falloff = 1.0f - glm::distance(pos, world.lastCameraPos) / HEARING_DISTANCE;
    play(hiss, nextVoice[2], 0.5f * falloff, 2.6f + (rand01() - rand01()) * 0.8f);
}

void LavaEffects::update(const World& world, glm::vec3 listener, float dt) {
    // Block N spans [N-0.5, N+0.5); smoke starts on the hardened block's top.
    for (const glm::ivec3& b : world.waterSimulator->fizzes) fizz(world, glm::vec3(b) + glm::vec3(0.0f, 0.5f, 0.0f));
    world.waterSimulator->fizzes.clear();

    accumulator += dt;
    if (accumulator < 0.05f) return;
    accumulator = std::fmod(accumulator, 0.05f); // one pass per frame at most, even after a hitch
    fizzesThisTick = 0;

    WorldResolver resolver(world.chunkManager);
    glm::ivec3 c(glm::floor(listener + 0.5f));
    std::uniform_int_distribution<int> offset(-SAMPLE_RADIUS, SAMPLE_RADIUS);
    for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
        int x = c.x + offset(rng), y = c.y + offset(rng), z = c.z + offset(rng);
        if (y < 0 || y + 1 >= CHUNK_HEIGHT) continue;
        auto loc = resolver.local(x, z);
        if (!loc.chunk || loc.chunk->getBlockType(loc.lx, y, loc.lz) != LAVA) continue;
        if (loc.chunk->getBlockType(loc.lx, y + 1, loc.lz) != AIR) continue;

        bool popNow = std::uniform_int_distribution<int>(0, POP_CHANCE - 1)(rng) == 0;
        bool bubbleNow = std::uniform_int_distribution<int>(0, AMBIENT_CHANCE - 1)(rng) == 0;
        if (!popNow && !bubbleNow) continue;
        glm::vec3 surface(x + rand01() - 0.5f, y + 0.4f, z + rand01() - 0.5f);
        float falloff = 1.0f - glm::distance(surface, listener) / HEARING_DISTANCE;
        // Minecraft: both at volume 0.2..0.4, pitch 0.9..1.05.
        if (popNow) {
            world.particles->spawnLavaEmber(surface);
            play(pop, nextVoice[1], (0.2f + rand01() * 0.2f) * falloff, 0.9f + rand01() * 0.15f);
        }
        if (bubbleNow) play(ambient, nextVoice[0], (0.2f + rand01() * 0.2f) * falloff, 0.9f + rand01() * 0.15f);
    }
}
