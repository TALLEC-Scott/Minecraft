#include "entity_manager.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <miniaudio.h>

#include "entity_cube_renderer.h"
#include "shader.h"
#include "world.h"

EntityManager::~EntityManager() {
    if (audioLoaded) {
        for (int i = 0; i < NUM_EXPLODE_SOUNDS; ++i) {
            if (explodeSounds[i]) {
                ma_sound_uninit(explodeSounds[i]);
                delete explodeSounds[i];
                explodeSounds[i] = nullptr;
            }
        }
    }
}

void EntityManager::initAudio(ma_engine* engine) {
    if (!engine || audioLoaded) return;
    for (int i = 0; i < NUM_EXPLODE_SOUNDS; ++i) {
        explodeSounds[i] = new ma_sound{};
        std::string path = "assets/Sounds/tnt/explode" + std::to_string(i + 1) + ".wav";
        ma_result r =
            ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, explodeSounds[i]);
        if (r != MA_SUCCESS) {
            delete explodeSounds[i];
            explodeSounds[i] = nullptr;
            continue;
        }
        ma_sound_set_looping(explodeSounds[i], MA_FALSE);
        ma_sound_set_volume(explodeSounds[i], 1.0f);
    }
    audioLoaded = true;
}

void EntityManager::playExplosion(glm::vec3 /*pos*/) {
    if (!audioLoaded) return;
    int idx = std::rand() % NUM_EXPLODE_SOUNDS;
    ma_sound* s = explodeSounds[idx];
    if (!s) return;
    // Rewind + start. miniaudio serializes playback per-sound, so a second
    // explosion in quick succession will cut off the first — acceptable for
    // TNT; chain reactions still sound like a rolling boom.
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}

void EntityManager::spawnTnt(glm::vec3 pos, int fuse, double now) {
    tntEntities.emplace_back(std::make_unique<TntEntity>(pos, fuse, now));
}

void EntityManager::tick(World* world, float dt, double now) {
    accumulator += dt;
    // Cap catch-up so a long pause (e.g. menu, breakpoint) can't trigger
    // hundreds of ticks at once.
    if (accumulator > 0.5) accumulator = 0.5;
    while (accumulator >= TICK_SECONDS) {
        accumulator -= TICK_SECONDS;
        // Iterate by index — detonation may push new entities onto the
        // vector (chain reaction), and we must not tick those in the same
        // pass (they keep their fresh fuse countdown). Hold raw pointers
        // rather than references: spawnTnt() from inside explode() can
        // reallocate the vector, which would dangle a `unique_ptr&`.
        size_t n = tntEntities.size();
        for (size_t i = 0; i < n; ++i) {
            TntEntity* e = tntEntities[i].get();
            if (e->dead) continue;
            if (e->tick(world)) {
                // Cache pos before explode — spawnTnt() may realloc the
                // vector; the TntEntity heap object itself is stable
                // (unique_ptr move just transfers the pointer).
                glm::vec3 detonationPos = e->pos;
                e->dead = true;
                if (world) world->explode(detonationPos, 4.0f, now);
            }
        }
        tntEntities.erase(std::remove_if(tntEntities.begin(), tntEntities.end(),
                                         [](const std::unique_ptr<TntEntity>& e) { return e->dead; }),
                          tntEntities.end());
    }
}

void EntityManager::render(const Shader& shader, glm::mat4 viewProjection, EntityCubeRenderer& cubes, double now) {
    for (auto& e : tntEntities) {
        if (e->dead) continue;
        cubes.drawTnt(shader, viewProjection, e->pos, e->flashBrightness(now));
    }
}

size_t EntityManager::liveCount() const {
    size_t n = 0;
    for (auto& e : tntEntities)
        if (!e->dead) ++n;
    return n;
}
