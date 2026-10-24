#pragma once

// =====================================================================
// Mob registry — the single table every mob type is declared in.
//
// HOW TO ADD A MOB (e.g. "Cow")
// -----------------------------
//  1. include/mobs/cow.h + src/mobs/cow.cpp:
//       class Cow : public Mob {
//         public:
//           Cow();                                   // : Mob(MobType::Cow)
//           const MobModel& model() const override;  // static model, built once
//           MobPose pose(float partialTick) const override;
//       };
//     In the constructor register AI goals (see mob_ai.h), e.g.
//       goals.add(0, std::make_unique<PanicGoal>(0.2f));
//       goals.add(5, std::make_unique<WanderGoal>(0.09f));
//       goals.add(6, std::make_unique<LookAtPlayerGoal>(8.0f));
//     Override hooks as needed: aiTick() (call Mob::aiTick first to run
//     the goals), onHurt(), onDeath(), customTick(), fallDamageMultiplier(),
//     floatsInWater(). src/mobs/pig.cpp is the reference implementation.
//  2. Add `Cow` to `enum class MobType` below (before `Count`).
//  3. Add one row to MOB_TABLE in src/mob_registry.cpp (same order as the
//     enum — a static_assert + the registry test catch mismatches) and
//     `#include "mobs/cow.h"` at the top of that file.
//  4. Add src/mobs/cow.cpp to COMMON_SOURCES in CMakeLists.txt (and to the
//     tests target if you write tests for it — mob code is GL-free).
//  5. Optional sounds: drop WAV files at assets/Sounds/mob/cow/<event>N.wav
//     (event = ambient | hurt | death | step, N = 1..4). Missing files are
//     silently ignored, see mob_sounds.h.
//
// That's it: natural spawning (biome mask + group size + category caps),
// the F6/F7 debug spawner, rendering, combat and despawning all read the
// table — nothing else needs touching.
// =====================================================================

#include <cstdint>
#include <memory>

#include "cube.h" // Biome

class Mob;

enum class MobType : uint8_t {
    Pig,
    Chicken,
    Creeper,
    Spider,
    Cow,
    Sheep,
    Count // keep last
};

inline constexpr int MOB_TYPE_COUNT = static_cast<int>(MobType::Count);

enum class MobCategory : uint8_t {
    Passive, // spawns on grass in light >= 9, never despawns while loaded
    Hostile, // spawns in light <= 7, despawns far from the player
};

// Bit for one biome in MobInfo::spawnBiomes.
constexpr uint32_t biomeBit(Biome b) {
    return 1u << static_cast<uint32_t>(b);
}
inline constexpr uint32_t ALL_LAND_BIOMES =
    biomeBit(BIOME_BEACH) | biomeBit(BIOME_PLAINS) | biomeBit(BIOME_FOREST) | biomeBit(BIOME_DESERT) |
    biomeBit(BIOME_TUNDRA);

using MobFactory = std::unique_ptr<Mob> (*)();

// One row of the registry. Hitbox + health are copied into the Mob by its
// base constructor, so a subclass only has to pass its MobType.
struct MobInfo {
    MobType type;
    const char* name; // lowercase id: debug label + sound folder (assets/Sounds/mob/<name>/)
    MobFactory create;
    MobCategory category;
    float halfWidth; // hitbox half-extent on X/Z (Minecraft width / 2)
    float height;    // hitbox height
    float maxHealth; // in half-hearts (player has 20)
    uint32_t spawnBiomes; // biomeBit() mask; 0 = never spawns naturally (debug key only)
    int groupMin;         // natural-spawn pack size
    int groupMax;
    int spawnWeight; // relative pick weight among eligible types of the same category
};

// Row for `type`. Asserts in debug if out of range.
const MobInfo& mobInfo(MobType type);
// The whole table, indexed by MobType (size MOB_TYPE_COUNT).
const MobInfo* mobRegistry();
// Instantiate a mob by type (calls the row's factory). Position is (0,0,0);
// the caller (normally MobManager::spawnMob) places it.
std::unique_ptr<Mob> createMob(MobType type);
const char* mobName(MobType type);
