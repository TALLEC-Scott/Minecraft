#include "mob_manager.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "frustum.h"
#include "mob_spawn.h"
#include "shader.h"
#include "tracy_shim.h"
#include "world.h"

// ---------------------------------------------------------------------------
// WorldBlocks — MobBlockAccess backed by the live ChunkManager. Caches the
// last chunk; valid for one tick (chunks only load/unload in
// ChunkManager::update, never during the mob tick).
// ---------------------------------------------------------------------------

class MobManager::WorldBlocks : public MobBlockAccess {
  public:
    explicit WorldBlocks(World* w) : world(w) {}

    void invalidate() {
        cachedCx = INT_MIN;
        cachedCz = INT_MIN;
        cached = nullptr;
    }

    block_type blockAt(int x, int y, int z) const override {
        if (y < 0 || y >= CHUNK_HEIGHT) return AIR;
        int lx = 0, lz = 0;
        Chunk* c = chunkFor(x, z, lx, lz);
        return c ? c->getBlockType(lx, y, lz) : AIR;
    }

    bool isLoaded(int x, int z) const override {
        int lx = 0, lz = 0;
        return chunkFor(x, z, lx, lz) != nullptr;
    }

    int skyLightAt(int x, int y, int z) const override {
        if (y >= CHUNK_HEIGHT) return 15;
        if (y < 0) return 0;
        int lx = 0, lz = 0;
        Chunk* c = chunkFor(x, z, lx, lz);
        return c ? c->getSkyLight(lx, y, lz) : 15;
    }

    int blockLightAt(int x, int y, int z) const override {
        if (y < 0 || y >= CHUNK_HEIGHT) return 0;
        int lx = 0, lz = 0;
        Chunk* c = chunkFor(x, z, lx, lz);
        return c ? c->getBlockLight(lx, y, lz) : 0;
    }

    Biome biomeAt(int x, int z) const override { return world->terrainGenerator->getBiome(x, z); }

  private:
    World* world;
    mutable int cachedCx = INT_MIN, cachedCz = INT_MIN;
    mutable Chunk* cached = nullptr;

    Chunk* chunkFor(int x, int z, int& lx, int& lz) const {
        int cx = worldToChunk(x), cz = worldToChunk(z);
        if (cx != cachedCx || cz != cachedCz) {
            cachedCx = cx;
            cachedCz = cz;
            cached = world->chunkManager->getChunk(cx, cz);
        }
        lx = worldToLocal(x, cx);
        lz = worldToLocal(z, cz);
        return cached;
    }
};

// ---------------------------------------------------------------------------

MobManager::MobManager(World* w, unsigned int seed)
    : world(w), blocks(std::make_unique<WorldBlocks>(w)), rng(seed ^ 0x6D0B5u) {}

MobManager::~MobManager() = default;

void MobManager::setSunHeight(float sunHeight) {
    isDay = mobspawn::isDaytime(sunHeight);
    skyDarken = mobspawn::skyDarkenFromSunHeight(sunHeight);
}

void MobManager::initAudio(ma_engine* engine) {
    sounds.init(engine);
}

MobContext MobManager::makeContext() {
    MobContext ctx;
    ctx.world = world;
    ctx.blocks = blocks.get();
    ctx.rng = &rng;
    ctx.playerFeet = player.feet;
    ctx.playerEye = player.eye;
    ctx.playerVelocity = player.velocity;
    ctx.playerTargetable = playerKnown && player.targetable;
    ctx.isDay = isDay;
    ctx.skyDarken = skyDarken;
    ctx.gameTick = tickCount;
    ctx.mobs = &mobList;
    ctx.onDamagePlayer = [this](float amount, glm::vec3 from) {
        if (damagePlayerCb) damagePlayerCb(amount, from);
    };
    ctx.onExplode = [this](glm::vec3 center, float power) {
        if (world) world->explode(center, power, lastNow);
    };
    ctx.onSpawnProjectile = [this](const ProjectileSpec& spec) { spawnProjectile(spec); };
    ctx.onSound = [this](const Mob& m, MobSoundEvent ev) { sounds.play(m.type, ev, m.pos, player.eye); };
    return ctx;
}

void MobManager::update(float dt, double now) {
    lastNow = now;
    accumulator += dt;
    // Same catch-up cap as EntityManager: a long hitch can't burst-tick.
    if (accumulator > 0.5) accumulator = 0.5;
    while (accumulator >= TICK_SECONDS) {
        accumulator -= TICK_SECONDS;
        tickOnce();
    }
}

void MobManager::tickOnce() {
    ++tickCount;
    blocks->invalidate();
    MobContext ctx = makeContext();

    // Index loop with a size snapshot: mob code may spawn (debug, future
    // breeding) mid-tick; new mobs start ticking next tick. Raw pointers
    // stay valid across a vector reallocation (unique_ptr owns the heap
    // object), so hold Mob* rather than a reference into the vector.
    size_t n = mobList.size();
    for (size_t i = 0; i < n; ++i) {
        Mob* m = mobList[i].get();
        if (m->removed) continue;
        glm::ivec3 b = MobContext::blockAtPos(m->pos);
        if (!blocks->isLoaded(b.x, b.z)) {
            m->removed = true; // chunk streamed out — mobs aren't persisted
            continue;
        }
        m->tick(ctx);
        flushSounds(*m);
    }

    size_t np = projectileList.size();
    for (size_t i = 0; i < np; ++i) {
        Projectile* p = projectileList[i].get();
        if (p->removed) continue;
        glm::ivec3 b = MobContext::blockAtPos(p->pos);
        if (!blocks->isLoaded(b.x, b.z)) {
            p->removed = true;
            continue;
        }
        p->tick(ctx, &mobList);
    }
    // Arrow hits can hurt mobs after their tick — play those sounds too.
    for (auto& m : mobList)
        if (m->pendingSounds) flushSounds(*m);

    despawn();

    if (spawningEnabled && playerKnown) {
        if (tickCount % PASSIVE_SPAWN_INTERVAL == 0) trySpawnGroup(ctx, MobCategory::Passive);
        if (tickCount % HOSTILE_SPAWN_INTERVAL == 0) trySpawnGroup(ctx, MobCategory::Hostile);
    }
    if (cowRain && playerKnown && tickCount % COW_RAIN_INTERVAL == 0) dropRainingCow(ctx);

    mobList.erase(std::remove_if(mobList.begin(), mobList.end(), [](const auto& m) { return m->removed; }),
                  mobList.end());
    projectileList.erase(
        std::remove_if(projectileList.begin(), projectileList.end(), [](const auto& p) { return p->removed; }),
        projectileList.end());
}

void MobManager::despawn() {
    if (!playerKnown) return;
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    for (auto& m : mobList) {
        if (m->removed || m->persistent || m->category() != MobCategory::Hostile) continue;
        float d = glm::distance(m->pos, player.feet);
        if (mobspawn::shouldDespawnHostile(d, roll(rng))) m->removed = true;
    }
}

void MobManager::trySpawnGroup(MobContext& ctx, MobCategory category) {
    int cap = (category == MobCategory::Passive) ? PASSIVE_CAP : HOSTILE_CAP;
    if (countNear(category, player.feet, CAP_RADIUS) >= cap) return;
    if (static_cast<int>(mobList.size()) >= MAX_MOBS) return;

    // Random column in the 24..64 ring around the player.
    float ang = ctx.randf() * glm::two_pi<float>();
    float r = mobspawn::SPAWN_MIN_DIST + ctx.randf() * (mobspawn::SPAWN_MAX_DIST - mobspawn::SPAWN_MIN_DIST);
    int x = static_cast<int>(std::floor(player.feet.x + std::cos(ang) * r + 0.5f));
    int z = static_cast<int>(std::floor(player.feet.z + std::sin(ang) * r + 0.5f));
    if (!blocks->isLoaded(x, z)) return;

    std::vector<MobType> types = mobspawn::eligibleTypes(category, blocks->biomeAt(x, z));
    if (types.empty()) return;
    MobType type = mobspawn::pickWeighted(types, ctx.randf());
    if (type == MobType::Count) return;
    const MobInfo& info = mobInfo(type);

    // Surface = first opaque-solid or water block from the top (leaves are
    // skipped so forest floors are reachable).
    int surface = -1;
    for (int y = CHUNK_HEIGHT - 1; y > 0; --y) {
        block_type t = blocks->blockAt(x, y, z);
        if (t == WATER || (hasFlag(t, BF_SOLID) && hasFlag(t, BF_OPAQUE))) {
            surface = y;
            break;
        }
    }
    if (surface < 0) return;

    int baseY = surface + 1;
    if (category == MobCategory::Hostile) {
        // Anywhere in the column at or below the surface (caves included):
        // pick a height, then drop to the first floor below it.
        int y = ctx.randInt(1, surface + 1);
        while (y > 1 && !(ctx.isSolid(x, y - 1, z) && !ctx.isSolid(x, y, z))) --y;
        baseY = y;
    }

    int groupSize = ctx.randInt(info.groupMin, info.groupMax);
    int placed = 0;
    for (int attempt = 0; attempt < groupSize * 4 && placed < groupSize; ++attempt) {
        int gx = x, gz = z;
        if (attempt > 0) {
            gx += ctx.randInt(-4, 4);
            gz += ctx.randInt(-4, 4);
        }
        glm::vec3 feet;
        if (!mobai::findStandY(ctx, gx, gz, baseY, info.halfWidth, info.height, 2, 2, feet)) continue;
        int gy = MobContext::blockAtPos(feet).y;
        if (!mobspawn::inSpawnRing(player.feet, feet)) continue;
        if (!mobspawn::canSpawnAt(*blocks, info, gx, gy, gz, skyDarken)) continue;
        if (Mob* m = spawnMob(type, feet, false)) {
            m->yaw = m->prevYaw = m->headYaw = m->prevHeadYaw = ctx.randf() * 360.0f - 180.0f;
            ++placed;
        }
    }
}

void MobManager::dropRainingCow(MobContext& ctx) {
    float ang = ctx.randf() * glm::two_pi<float>();
    float r = std::sqrt(ctx.randf()) * COW_RAIN_RADIUS; // uniform over the disc
    float x = std::floor(player.feet.x + std::cos(ang) * r + 0.5f);
    float z = std::floor(player.feet.z + std::sin(ang) * r + 0.5f);
    int bx = static_cast<int>(x), bz = static_cast<int>(z);
    if (!blocks->isLoaded(bx, bz)) return;
    const MobInfo& info = mobInfo(MobType::CreeperCow);
    float y = std::min(player.feet.y + COW_RAIN_HEIGHT, static_cast<float>(CHUNK_HEIGHT) - info.height - 1.0f);
    // Skip drop points inside terrain (e.g. the player is in a cave).
    int by = MobContext::blockAtPos(glm::vec3(x, y, z)).y;
    for (int dy = 0; dy <= static_cast<int>(std::ceil(info.height)); ++dy)
        if (ctx.isSolid(bx, by + dy, bz)) return;
    if (Mob* m = spawnMob(MobType::CreeperCow, glm::vec3(x, y, z), false))
        m->yaw = m->prevYaw = m->headYaw = m->prevHeadYaw = ctx.randf() * 360.0f - 180.0f;
}

Mob* MobManager::spawnMob(MobType type, glm::vec3 feetPos, bool persistent) {
    if (static_cast<int>(mobList.size()) >= MAX_MOBS) return nullptr;
    std::unique_ptr<Mob> m = createMob(type);
    m->id = nextId++;
    m->pos = m->prevPos = feetPos;
    m->persistent = persistent;
    Mob* raw = m.get();
    mobList.push_back(std::move(m));
    return raw;
}

void MobManager::spawnProjectile(const ProjectileSpec& spec) {
    projectileList.push_back(std::make_unique<Projectile>(spec));
}

Mob* MobManager::raycast(glm::vec3 origin, glm::vec3 dir, float maxDist, float& outDist) const {
    glm::vec3 d = glm::normalize(dir);
    Mob* best = nullptr;
    float bestT = maxDist;
    for (const auto& m : mobList) {
        if (!m->isAlive()) continue;
        float t = 0.0f;
        if (m->rayHit(origin, d, bestT, t) && t <= bestT) {
            bestT = t;
            best = m.get();
        }
    }
    if (best) outDist = bestT;
    return best;
}

bool MobManager::attackMob(Mob& mob, float amount, glm::vec3 attackerPos, float knockbackScale) {
    bool hit = mob.damage(amount, attackerPos, knockbackScale);
    flushSounds(mob);
    return hit;
}

void MobManager::applyExplosion(glm::vec3 center, float power) {
    // Minecraft Explosion: impact = (1 - dist / (2 * power)) * exposure
    // (exposure taken as 1 — no occlusion rays), damage =
    // (impact² + impact) / 2 * 7 * 2 * power + 1.
    float radius = power * 2.0f;
    auto damageFor = [&](float dist, float& impact) {
        impact = 1.0f - dist / radius;
        return (impact * impact + impact) * 0.5f * 7.0f * radius + 1.0f;
    };
    for (auto& m : mobList) {
        if (!m->isAlive()) continue;
        float dist = glm::distance(m->center(), center);
        if (dist >= radius) continue;
        float impact = 0.0f;
        float dmg = damageFor(dist, impact);
        m->damage(dmg, center, impact * 2.5f);
        flushSounds(*m);
    }
    if (playerKnown && player.targetable && damagePlayerCb) {
        glm::vec3 body = player.feet + glm::vec3(0.0f, 0.9f, 0.0f);
        float dist = glm::distance(body, center);
        if (dist < radius) {
            float impact = 0.0f;
            damagePlayerCb(damageFor(dist, impact), center);
        }
    }
}

void MobManager::clear() {
    mobList.clear();
    projectileList.clear();
}

int MobManager::countNear(MobCategory category, glm::vec3 c, float radius) const {
    int n = 0;
    float r2 = radius * radius;
    for (const auto& m : mobList) {
        if (m->removed || m->category() != category) continue;
        glm::vec3 d = m->pos - c;
        if (glm::dot(d, d) <= r2) ++n;
    }
    return n;
}

void MobManager::flushSounds(Mob& mob) {
    uint8_t bits = mob.pendingSounds;
    mob.pendingSounds = 0;
    if (!bits) return;
    for (int e = 0; e < static_cast<int>(MobSoundEvent::Count); ++e)
        if (bits & (1u << e)) sounds.play(mob.type, static_cast<MobSoundEvent>(e), mob.pos, player.eye);
}

void MobManager::render(const Shader& shader, const glm::mat4& viewProjection, glm::vec3 cameraPos) {
    if (mobList.empty() && projectileList.empty()) return;
    ZoneScopedN("Mobs::render");
    auto planes = extractFrustumPlanes(viewProjection);
    float pt = partialTick();
    blocks->invalidate();
    auto lightAt = [&](glm::vec3 p) {
        glm::ivec3 b = MobContext::blockAtPos(p);
        return static_cast<float>(blocks->skyLightAt(b.x, b.y, b.z) * 16 + blocks->blockLightAt(b.x, b.y, b.z));
    };
    constexpr float maxD2 = MOB_RENDER_DISTANCE * MOB_RENDER_DISTANCE;

    for (const auto& m : mobList) {
        if (m->removed) continue;
        glm::vec3 p = m->renderPos(pt);
        glm::vec3 d = p - cameraPos;
        if (glm::dot(d, d) > maxD2) continue;
        // Generous box: models can overhang the hitbox (heads, death tip-over).
        float r = std::max(m->halfWidth, m->height) + 0.5f;
        if (!aabbInFrustum(planes, p - glm::vec3(r, 0.5f, r), p + glm::vec3(r, m->height + 0.5f, r))) continue;
        // Sample at the body center and the eyes and keep the brighter of
        // each channel: a single sample goes black whenever that block is
        // solid (a mob pressed into a wall, a low eye level on a ledge).
        float center = lightAt(p + glm::vec3(0.0f, m->height * 0.5f, 0.0f));
        float eye = lightAt(m->eyePos());
        float sky = std::max(std::floor(center / 16.0f), std::floor(eye / 16.0f));
        float block = std::max(std::fmod(center, 16.0f), std::fmod(eye, 16.0f));
        renderer.drawMob(shader, *m, pt, sky * 16.0f + block);
    }

    for (const auto& pr : projectileList) {
        glm::vec3 p = glm::mix(pr->prevPos, pr->pos, pt);
        glm::vec3 d = p - cameraPos;
        if (glm::dot(d, d) > maxD2) continue;
        if (!aabbInFrustum(planes, p - glm::vec3(0.5f), p + glm::vec3(0.5f))) continue;
        // Orient the box's +Z along the flight direction.
        glm::vec3 dir = pr->direction();
        glm::mat4 m = glm::translate(glm::mat4(1.0f), p);
        m = glm::rotate(m, std::atan2(dir.x, dir.z), glm::vec3(0, 1, 0));
        m = glm::rotate(m, -std::asin(std::clamp(dir.y, -1.0f, 1.0f)), glm::vec3(1, 0, 0));
        m = glm::scale(m, glm::vec3(0.06f, 0.06f, 0.6f));
        renderer.drawBox(shader, m, pr->spec.color, lightAt(p));
    }
}
