#pragma once

// Natural spawn / despawn rules as pure functions over MobBlockAccess so
// they're unit-testable. MobManager drives them (ring sampling, caps,
// group placement); the rules below decide whether ONE spot is valid.
//
// Coordinates: (x, y, z) is the FEET block (the air cell the mob's feet
// occupy); the ground block is y - 1.

#include <vector>

#include <glm/glm.hpp>

#include "mob_context.h"
#include "mob_registry.h"

namespace mobspawn {

inline constexpr int PASSIVE_MIN_LIGHT = 9; // raw light (time-independent), Minecraft animals
inline constexpr int HOSTILE_MAX_LIGHT = 7; // effective light (night or caves)
inline constexpr float SPAWN_MIN_DIST = 24.0f;
inline constexpr float SPAWN_MAX_DIST = 64.0f;
inline constexpr float DESPAWN_INSTANT_DIST = 128.0f;
inline constexpr float DESPAWN_RANDOM_DIST = 32.0f;
inline constexpr float DESPAWN_RANDOM_CHANCE = 1.0f / 800.0f; // per tick, beyond DESPAWN_RANDOM_DIST

// Minecraft's sky darkening (0 = noon … 11 = midnight) from the sun's
// height (sin of the sun angle, -1..1).
int skyDarkenFromSunHeight(float sunHeight);
inline bool isDaytime(float sunHeight) {
    return sunHeight > 0.0f;
}

bool biomeAllows(const MobInfo& info, Biome biome);

// Light gates. `sky`/`block` are stored nibbles (0-15).
bool passiveLightOk(int sky, int block);
bool hostileLightOk(int sky, int block, int skyDarken);

// Hitbox fits at feet block (x, y, z): no solid overlap, feet + head cells
// not water. Unloaded columns count as solid.
bool hasRoom(const MobBlockAccess& blocks, const MobInfo& info, int x, int y, int z);

// Full per-spot check for `info`: biome, ground block, room, light.
bool canSpawnAt(const MobBlockAccess& blocks, const MobInfo& info, int x, int y, int z, int skyDarken);

bool inSpawnRing(glm::vec3 player, glm::vec3 pos);

// Hostile despawn decision. `roll` is a uniform [0,1) sample.
bool shouldDespawnHostile(float distToPlayer, float roll);

// Registry rows of `category` that may spawn in `biome`.
std::vector<MobType> eligibleTypes(MobCategory category, Biome biome);
// Weighted pick by MobInfo::spawnWeight; `roll` in [0,1). Empty → Count.
MobType pickWeighted(const std::vector<MobType>& candidates, float roll);

} // namespace mobspawn
