#include "entity_manager.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <miniaudio.h>

#include "entity_cube_renderer.h"
#include "inventory_model.h"
#include "shader.h"
#include "world.h"
#include "world_resolver.h"

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
        std::string path = "assets/Sounds/tnt/explode" + std::to_string(i + 1) + ".ogg";
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

void EntityManager::spawnItem(ItemId id, int count, glm::vec3 pos, glm::vec3 velocity, int pickupDelay) {
    if (id == ITEM_EMPTY || count <= 0) return;
    if (itemEntities.size() >= MAX_ITEMS) itemEntities.erase(itemEntities.begin()); // oldest first
    ItemEntity e;
    e.id = id;
    e.count = count;
    e.pos = e.prevPos = pos;
    e.velocity = velocity;
    e.pickupDelay = pickupDelay;
    e.spinPhase = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng);
    itemEntities.push_back(e);
}

void EntityManager::dropFromBlock(ItemId id, int count, glm::ivec3 block) {
    // Minecraft: anywhere in the middle half of the block, a small random
    // horizontal kick and a hop upwards.
    std::uniform_real_distribution<float> jitter(-0.25f, 0.25f), kick(-0.05f, 0.05f);
    glm::vec3 pos = glm::vec3(block) + glm::vec3(jitter(rng), -0.25f + jitter(rng), jitter(rng));
    spawnItem(id, count, pos, glm::vec3(kick(rng), 0.2f, kick(rng)));
}

void EntityManager::throwItem(ItemId id, int count, glm::vec3 eyePos, glm::vec3 dir) {
    glm::vec3 pos = eyePos - glm::vec3(0.0f, 0.3f, 0.0f);
    spawnItem(id, count, pos, dir * 0.3f + glm::vec3(0.0f, 0.1f, 0.0f), ItemEntity::THROW_DELAY);
}

void EntityManager::tickItems(World* world) {
    if (itemEntities.empty()) return;
    WorldResolver resolver(world ? world->chunkManager : nullptr);
    auto isSolid = [&](int bx, int by, int bz) -> bool {
        if (!world) return by < 0;
        if (by < 0 || by >= CHUNK_HEIGHT) return by < 0; // below the world is a floor
        auto loc = resolver.local(bx, bz);
        return loc.chunk && hasFlag(loc.chunk->getBlockType(loc.lx, by, loc.lz), BF_SOLID);
    };
    for (ItemEntity& e : itemEntities) {
        if (e.removed) continue;
        // Like mobs, drops in chunks that unloaded are simply gone.
        if (world && !resolver.local((int)std::floor(e.pos.x + 0.5f), (int)std::floor(e.pos.z + 0.5f)).chunk) {
            e.removed = true;
            continue;
        }
        e.tick(isSolid);
        // Items that fall into lava burn up, as in Minecraft.
        glm::ivec3 b = glm::ivec3(glm::floor(e.pos + glm::vec3(0.5f, 0.5f + ItemEntity::HEIGHT * 0.5f, 0.5f)));
        if (world && b.y >= 0 && b.y < CHUNK_HEIGHT) {
            auto loc = resolver.local(b.x, b.z);
            if (loc.chunk && loc.chunk->getBlockType(loc.lx, b.y, loc.lz) == LAVA) e.removed = true;
        }
    }
    // Merge neighbouring stacks of the same item, then hand reachable ones
    // to the player.
    for (size_t i = 0; i < itemEntities.size(); ++i)
        for (size_t j = i + 1; j < itemEntities.size(); ++j)
            mergeItems(itemEntities[i], itemEntities[j], maxStackSize(itemEntities[i].id));
    if (pickup) {
        for (ItemEntity& e : itemEntities) {
            if (!e.canBePickedUp()) continue;
            e.count -= std::min(pickup(e), e.count);
            if (e.count <= 0) e.removed = true;
        }
    }
    itemEntities.erase(std::remove_if(itemEntities.begin(), itemEntities.end(),
                                      [](const ItemEntity& e) { return e.removed; }),
                       itemEntities.end());
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
        tickItems(world);
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
