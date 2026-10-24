#pragma once

// Everything a mob can see or do during its 20-TPS tick, bundled so mob
// code never reaches into World / ChunkManager / Player directly. That
// keeps mob physics + AI GL-free and unit-testable: tests build a
// MobContext over a fake MobBlockAccess and leave the hooks empty.

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "cube.h"

class World;
class Mob;

// Read-only block/light/biome queries. Production: MobManager wraps the
// ChunkManager + TerrainGenerator. Tests: a map-backed fake.
class MobBlockAccess {
  public:
    virtual ~MobBlockAccess() = default;
    // Block at world block coords (block N spans [N-0.5, N+0.5)). AIR outside
    // the vertical range or in unloaded chunks — check isLoaded() first when
    // the difference matters.
    virtual block_type blockAt(int x, int y, int z) const = 0;
    virtual bool isLoaded(int x, int z) const = 0;
    // Stored light nibbles (0-15). Sky light is NOT time-of-day adjusted;
    // use MobContext::lightAt() for the effective value.
    virtual int skyLightAt(int x, int y, int z) const = 0;
    virtual int blockLightAt(int x, int y, int z) const = 0;
    virtual Biome biomeAt(int x, int z) const = 0;
};

// Mob sound events. Played through MobContext::sound(); files are looked up
// at assets/Sounds/mob/<name>/<event>N.wav (see mob_sounds.h).
enum class MobSoundEvent : uint8_t { Ambient, Hurt, Death, Step, Attack, Count };

// Arrow-like projectile request (skeletons, …). Physics is per tick, same
// units as Mob::velocity (blocks / tick).
struct ProjectileSpec {
    glm::vec3 pos{0.0f};
    glm::vec3 velocity{0.0f};
    float damage = 2.0f;
    float gravity = 0.05f; // per-tick, Minecraft arrow value
    float drag = 0.99f;    // per-tick velocity retention
    int lifetimeTicks = 1200;
    bool hurtsPlayer = true;
    bool hurtsMobs = false;
    uint32_t shooterId = 0; // mob id to ignore on launch (0 = none)
    glm::vec3 color{0.55f, 0.42f, 0.25f};
};

struct MobContext {
    World* world = nullptr;                 // null in unit tests — never required by mob logic
    const MobBlockAccess* blocks = nullptr; // always set
    std::mt19937* rng = nullptr;            // always set; deterministic when seeded by tests

    // Local player. `playerTargetable` is false while dead (or absent).
    glm::vec3 playerFeet{0.0f};
    glm::vec3 playerEye{0.0f};
    glm::vec3 playerVelocity{0.0f};
    bool playerTargetable = false;

    // Time of day. skyDarken is subtracted from stored sky light (0 at noon,
    // 11 at midnight — Minecraft's internal darkening), see lightAt().
    bool isDay = true;
    int skyDarken = 0;
    uint64_t gameTick = 0;

    // Other live mobs (may include the ticking mob itself). Null in tests.
    const std::vector<std::unique_ptr<Mob>>* mobs = nullptr;

    // --- Hooks (each may be empty; use the null-safe wrappers below) ---
    std::function<void(float amount, glm::vec3 fromPos)> onDamagePlayer;
    // Wraps World::explode (block destruction + entity/player damage).
    std::function<void(glm::vec3 center, float power)> onExplode;
    std::function<void(const ProjectileSpec&)> onSpawnProjectile;
    std::function<void(const Mob&, MobSoundEvent)> onSound;

    // --- Block helpers ---
    // Collision solidity. Unloaded chunks and below-world count as solid so
    // mobs never fall out of the world at a streaming edge.
    bool isSolid(int x, int y, int z) const;
    bool isWater(int x, int y, int z) const;
    // Effective light: max(blockLight, skyLight - skyDarken).
    int lightAt(int x, int y, int z) const;
    // Integer block containing a world point (handles the +0.5 centering).
    static glm::ivec3 blockAtPos(glm::vec3 p);

    // --- RNG helpers ---
    float randf();                // [0, 1)
    int randInt(int lo, int hi);  // inclusive
    bool chance(float p) { return randf() < p; }

    // --- Null-safe hook wrappers ---
    void damagePlayer(float amount, glm::vec3 fromPos) const {
        if (onDamagePlayer) onDamagePlayer(amount, fromPos);
    }
    void explode(glm::vec3 center, float power) const {
        if (onExplode) onExplode(center, power);
    }
    void spawnProjectile(const ProjectileSpec& spec) const {
        if (onSpawnProjectile) onSpawnProjectile(spec);
    }
    void sound(const Mob& mob, MobSoundEvent ev) const {
        if (onSound) onSound(mob, ev);
    }
};
