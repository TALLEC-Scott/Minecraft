#include "mob_registry.h"

#include <cassert>

#include "mob.h"
#include "mobs/cow.h"
#include "mobs/pig.h"
#include "mobs/sheep.h"

namespace {

template <typename T> std::unique_ptr<Mob> makeMob() {
    return std::make_unique<T>();
}

// One row per MobType, in enum order. Sizes are Minecraft's (width / 2,
// height); health in half-hearts. See mob_registry.h for the recipe.
// clang-format off
constexpr MobInfo MOB_TABLE[] = {
    // type        name   factory        category              halfW  height  hp     spawn biomes                                     group  weight
    {MobType::Pig, "pig", &makeMob<Pig>, MobCategory::Passive, 0.45f, 0.9f,   10.0f, biomeBit(BIOME_PLAINS) | biomeBit(BIOME_FOREST), 2, 4,  10},
    {MobType::Cow, "cow", &makeMob<Cow>, MobCategory::Passive, 0.45f, 1.4f,   10.0f, biomeBit(BIOME_PLAINS) | biomeBit(BIOME_FOREST) | biomeBit(BIOME_TUNDRA), 2, 4, 8},
    {MobType::Sheep, "sheep", &makeMob<Sheep>, MobCategory::Passive, 0.45f, 1.3f, 8.0f, biomeBit(BIOME_PLAINS) | biomeBit(BIOME_FOREST) | biomeBit(BIOME_TUNDRA), 2, 4, 12},
};
// clang-format on

static_assert(sizeof(MOB_TABLE) / sizeof(MOB_TABLE[0]) == MOB_TYPE_COUNT,
              "MOB_TABLE needs exactly one row per MobType (in enum order)");

} // namespace

const MobInfo& mobInfo(MobType type) {
    int idx = static_cast<int>(type);
    assert(idx >= 0 && idx < MOB_TYPE_COUNT);
    return MOB_TABLE[idx];
}

const MobInfo* mobRegistry() {
    return MOB_TABLE;
}

std::unique_ptr<Mob> createMob(MobType type) {
    return mobInfo(type).create();
}

const char* mobName(MobType type) {
    return mobInfo(type).name;
}
