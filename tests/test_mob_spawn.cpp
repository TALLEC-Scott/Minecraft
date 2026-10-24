#include <gtest/gtest.h>

#include <algorithm>

#include "mob_spawn.h"
#include "mob_test_support.h"

// Spawn rules are pure functions over MobBlockAccess. FakeBlocks reports a
// uniform light level / biome everywhere, set per test.

namespace {
const MobInfo& pigInfo() {
    return mobInfo(MobType::Pig);
}
// Grass floor at block row y = 4 → feet block y = 5.
void grassFloor(FakeBlocks& b) {
    b.fill(-4, 4, -4, 4, 4, 4, GRASS);
}
} // namespace

TEST(MobSpawnTest, SkyDarkenFollowsSun) {
    EXPECT_EQ(mobspawn::skyDarkenFromSunHeight(1.0f), 0);
    EXPECT_EQ(mobspawn::skyDarkenFromSunHeight(0.5f), 0);
    EXPECT_EQ(mobspawn::skyDarkenFromSunHeight(-1.0f), 11);
    int dusk = mobspawn::skyDarkenFromSunHeight(0.0f);
    EXPECT_GT(dusk, 0);
    EXPECT_LT(dusk, 11);
    EXPECT_TRUE(mobspawn::isDaytime(0.3f));
    EXPECT_FALSE(mobspawn::isDaytime(-0.3f));
}

TEST(MobSpawnTest, LightGates) {
    EXPECT_TRUE(mobspawn::passiveLightOk(9, 0));
    EXPECT_FALSE(mobspawn::passiveLightOk(8, 0));
    EXPECT_TRUE(mobspawn::passiveLightOk(0, 12)); // torch-lit (glowstone) counts
    // Open sky: lit at noon, dark at midnight.
    EXPECT_FALSE(mobspawn::hostileLightOk(15, 0, 0));
    EXPECT_TRUE(mobspawn::hostileLightOk(15, 0, 11));
    // Cave: no sky light → always dark enough unless a light source is near.
    EXPECT_TRUE(mobspawn::hostileLightOk(0, 0, 0));
    EXPECT_TRUE(mobspawn::hostileLightOk(0, 7, 0));
    EXPECT_FALSE(mobspawn::hostileLightOk(0, 8, 0));
}

TEST(MobSpawnTest, PigBiomes) {
    EXPECT_TRUE(mobspawn::biomeAllows(pigInfo(), BIOME_PLAINS));
    EXPECT_TRUE(mobspawn::biomeAllows(pigInfo(), BIOME_FOREST));
    EXPECT_FALSE(mobspawn::biomeAllows(pigInfo(), BIOME_DESERT));
    EXPECT_FALSE(mobspawn::biomeAllows(pigInfo(), BIOME_OCEAN));
}

TEST(MobSpawnTest, PassiveSpawnsOnLitGrass) {
    FakeBlocks b;
    grassFloor(b);
    EXPECT_TRUE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0));
}

TEST(MobSpawnTest, PassiveRejectsWrongGroundBiomeLightAndRoom) {
    {
        FakeBlocks b;
        b.fill(-4, 4, -4, 4, 4, 4, STONE);
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "not grass";
    }
    {
        FakeBlocks b;
        grassFloor(b);
        b.biome = BIOME_DESERT;
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "wrong biome";
    }
    {
        FakeBlocks b;
        grassFloor(b);
        b.sky = 4;
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "too dark";
    }
    {
        FakeBlocks b;
        grassFloor(b);
        b.set(0, 5, 0, STONE);
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "obstructed";
    }
    {
        FakeBlocks b;
        grassFloor(b);
        b.set(0, 5, 0, WATER);
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "in water";
    }
    {
        FakeBlocks b;
        grassFloor(b);
        b.loaded = false;
        EXPECT_FALSE(mobspawn::canSpawnAt(b, pigInfo(), 0, 5, 0, 0)) << "unloaded";
    }
}

TEST(MobSpawnTest, HasRoomUsesFullHitbox) {
    FakeBlocks b;
    grassFloor(b);
    // Pig is 0.9 wide: a block diagonally adjacent at feet level overlaps
    // the hitbox only if the box straddles it — centered spawns don't.
    b.set(1, 5, 1, STONE);
    EXPECT_TRUE(mobspawn::hasRoom(b, pigInfo(), 0, 5, 0));
    b.set(0, 6, 0, STONE); // pig height 0.9 fits under a block at y+1
    EXPECT_TRUE(mobspawn::hasRoom(b, pigInfo(), 0, 5, 0));
    b.set(0, 5, 0, TALL_GRASS); // non-solid plants don't block
    EXPECT_TRUE(mobspawn::hasRoom(b, pigInfo(), 0, 5, 0));
}

TEST(MobSpawnTest, SpawnRing) {
    glm::vec3 p(0.0f);
    EXPECT_FALSE(mobspawn::inSpawnRing(p, glm::vec3(10, 0, 0)));
    EXPECT_TRUE(mobspawn::inSpawnRing(p, glm::vec3(30, 0, 0)));
    EXPECT_TRUE(mobspawn::inSpawnRing(p, glm::vec3(0, 0, -64)));
    EXPECT_FALSE(mobspawn::inSpawnRing(p, glm::vec3(65, 0, 0)));
}

TEST(MobSpawnTest, HostileDespawnRules) {
    EXPECT_TRUE(mobspawn::shouldDespawnHostile(200.0f, 0.99f)) << "beyond 128 → instant";
    EXPECT_TRUE(mobspawn::shouldDespawnHostile(50.0f, 0.0f)) << "random despawn roll hit";
    EXPECT_FALSE(mobspawn::shouldDespawnHostile(50.0f, 0.5f));
    EXPECT_FALSE(mobspawn::shouldDespawnHostile(20.0f, 0.0f)) << "close mobs persist";
}

TEST(MobSpawnTest, EligibleTypesAndWeightedPick) {
    auto passive = mobspawn::eligibleTypes(MobCategory::Passive, BIOME_PLAINS);
    EXPECT_NE(std::find(passive.begin(), passive.end(), MobType::Pig), passive.end());
    auto desert = mobspawn::eligibleTypes(MobCategory::Passive, BIOME_DESERT);
    EXPECT_EQ(std::find(desert.begin(), desert.end(), MobType::Pig), desert.end());
    auto hostile = mobspawn::eligibleTypes(MobCategory::Hostile, BIOME_PLAINS);
    EXPECT_EQ(std::find(hostile.begin(), hostile.end(), MobType::Pig), hostile.end());

    EXPECT_EQ(mobspawn::pickWeighted({}, 0.5f), MobType::Count);
    EXPECT_EQ(mobspawn::pickWeighted({MobType::Pig}, 0.0f), MobType::Pig);
    EXPECT_EQ(mobspawn::pickWeighted({MobType::Pig}, 0.999f), MobType::Pig);
}

TEST(MobSpawnTest, FindStandYLocatesFloor) {
    MobHarness h;
    h.blocks.fill(-2, 4, -2, 2, 4, 2, GRASS);
    glm::vec3 feet;
    ASSERT_TRUE(mobai::findStandY(h.ctx, 0, 0, 7, 0.45f, 0.9f, 4, 4, feet));
    EXPECT_FLOAT_EQ(feet.y, 4.5f);
    EXPECT_FALSE(mobai::findStandY(h.ctx, 10, 10, 7, 0.45f, 0.9f, 2, 2, feet)) << "no floor in range";
}
