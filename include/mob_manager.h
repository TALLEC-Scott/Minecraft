#pragma once

// Owns every live mob + projectile in a World and drives them at 20 TPS.
//   - natural spawning around the player (mob_spawn.h rules, registry
//     biome/group/weight data, category caps) and hostile despawning
//   - render with distance + frustum culling, interpolated between ticks
//   - player <-> mob glue: melee raycast/attack, explosion damage, the
//     damage-player callback mobs reach through MobContext
//   - mob sounds (MobSoundBank, silent when no files are present)
// Mobs are local-only (not synced over NetSession) and not saved; a mob
// whose chunk unloads is dropped.

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "mob.h"
#include "mob_renderer.h"
#include "mob_sounds.h"
#include "projectile.h"

class World;
class Shader;
struct ma_engine;

class MobManager {
  public:
    static constexpr double TICK_SECONDS = 0.05; // 20 TPS
    static constexpr int PASSIVE_CAP = 10;       // counted within CAP_RADIUS of the player
    static constexpr int HOSTILE_CAP = 15;
    static constexpr float CAP_RADIUS = 128.0f;
    static constexpr int MAX_MOBS = 256;           // hard ceiling incl. debug spawns
    static constexpr int PASSIVE_SPAWN_INTERVAL = 20; // ticks between passive group attempts
    static constexpr int HOSTILE_SPAWN_INTERVAL = 5;
    static constexpr float MOB_RENDER_DISTANCE = 96.0f; // (RENDER_DISTANCE is a cube.h macro)

    struct PlayerState {
        glm::vec3 feet{0.0f};
        glm::vec3 eye{0.0f};
        glm::vec3 velocity{0.0f};
        bool targetable = false; // alive and in the world
    };

    MobManager(World* world, unsigned int seed);
    ~MobManager();
    MobManager(const MobManager&) = delete;
    MobManager& operator=(const MobManager&) = delete;

    // --- Per-frame inputs (main loop) ---
    // Natural spawning stays off until the first setPlayer() call.
    void setPlayer(const PlayerState& p) {
        player = p;
        playerKnown = true;
    }
    // sin(sun angle), -1..1. Drives isDay + sky darkening for spawn light.
    void setSunHeight(float sunHeight);
    void setDamagePlayerCallback(std::function<void(float amount, glm::vec3 fromPos)> cb) {
        damagePlayerCb = std::move(cb);
    }
    void setSpawningEnabled(bool on) { spawningEnabled = on; }
    void initAudio(ma_engine* engine);

    // Run 0..n 20-TPS ticks for `dt` seconds. `now` is wall-clock (for
    // World::explode's TNT flash phase).
    void update(float dt, double now);
    void render(const Shader& shader, const glm::mat4& viewProjection, glm::vec3 cameraPos);

    // --- Spawning ---
    // Debug / scripted spawn at a feet position. `persistent` mobs never
    // natural-despawn. Returns null when MAX_MOBS is reached.
    Mob* spawnMob(MobType type, glm::vec3 feetPos, bool persistent = true);
    void spawnProjectile(const ProjectileSpec& spec);

    // --- Combat ---
    // Nearest living mob whose hitbox the ray enters within `maxDist`.
    Mob* raycast(glm::vec3 origin, glm::vec3 dir, float maxDist, float& outDist) const;
    // Damage with knockback away from `attackerPos`; plays hurt/death sound.
    bool attackMob(Mob& mob, float amount, glm::vec3 attackerPos, float knockbackScale = 1.0f);
    // Minecraft explosion damage falloff over 2 * power blocks for mobs and
    // the player (through the damage-player callback).
    void applyExplosion(glm::vec3 center, float power);

    void clear();

    // --- Queries ---
    const std::vector<std::unique_ptr<Mob>>& mobs() const { return mobList; }
    const std::vector<std::unique_ptr<Projectile>>& projectiles() const { return projectileList; }
    int countNear(MobCategory category, glm::vec3 center, float radius) const;
    float partialTick() const { return static_cast<float>(accumulator / TICK_SECONDS); }
    uint64_t gameTick() const { return tickCount; }

  private:
    class WorldBlocks; // MobBlockAccess over ChunkManager + TerrainGenerator

    World* world;
    std::unique_ptr<WorldBlocks> blocks;
    std::vector<std::unique_ptr<Mob>> mobList;
    std::vector<std::unique_ptr<Projectile>> projectileList;
    std::mt19937 rng;
    MobRenderer renderer;
    MobSoundBank sounds;
    std::function<void(float, glm::vec3)> damagePlayerCb;

    PlayerState player;
    bool playerKnown = false;
    bool spawningEnabled = true;
    bool isDay = true;
    int skyDarken = 0;
    double accumulator = 0.0;
    double lastNow = 0.0;
    uint64_t tickCount = 0;
    uint32_t nextId = 1;

    MobContext makeContext();
    void tickOnce();
    void despawn();
    void trySpawnGroup(MobContext& ctx, MobCategory category);
    void flushSounds(Mob& mob);
};
