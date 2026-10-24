#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "mob_test_support.h"
#include "mobs/chicken.h"

// Chicken: registry row, flutter-fall, wing flap accumulators, egg timer.
// Same block-centered convention as test_mob_physics.cpp (floor top at
// y = 0.5).

TEST(ChickenTest, RegistryRow) {
    const MobInfo& info = mobInfo(MobType::Chicken);
    EXPECT_EQ(info.type, MobType::Chicken);
    EXPECT_STREQ(info.name, "chicken");
    EXPECT_EQ(info.category, MobCategory::Passive);
    EXPECT_FLOAT_EQ(info.halfWidth, 0.2f);
    EXPECT_FLOAT_EQ(info.height, 0.7f);
    EXPECT_FLOAT_EQ(info.maxHealth, 4.0f);
    EXPECT_EQ(info.spawnBiomes, biomeBit(BIOME_PLAINS) | biomeBit(BIOME_FOREST));
    EXPECT_EQ(info.groupMin, 2);
    EXPECT_EQ(info.groupMax, 4);
    EXPECT_EQ(info.spawnWeight, 10);

    auto mob = createMob(MobType::Chicken);
    ASSERT_NE(dynamic_cast<Chicken*>(mob.get()), nullptr);
    EXPECT_FLOAT_EQ(mob->health, 4.0f);
    const MobModel& m = mob->model();
    EXPECT_LE(m.size(), MAX_MOB_PARTS);
    for (const char* part : {"body", "head", "beak", "wattle", "eye_l", "eye_r", "wing_l", "wing_r", "leg_l", "leg_r"})
        EXPECT_GE(m.indexOf(part), 0) << part;
}

TEST(ChickenTest, SlowFallTerminalSpeed) {
    MobHarness h; // no floor → free fall
    Chicken c;
    TestMob t;
    c.pos = c.prevPos = glm::vec3(0.0f, 120.0f, 0.0f);
    t.pos = t.prevPos = glm::vec3(5.0f, 120.0f, 0.0f);
    h.run(c, 40);
    h.run(t, 40);
    // Steady state v = 0.6 * (v - 0.08) * 0.98 → v ≈ -0.114 blocks/tick.
    EXPECT_NEAR(c.velocity.y, -0.0784f * 0.6f / (1.0f - 0.588f), 1e-3f);
    EXPECT_LT(t.velocity.y, 10.0f * c.velocity.y) << "chicken should fall far slower than a normal mob";
}

TEST(ChickenTest, NoFallDamageFromTwentyBlocks) {
    MobHarness h;
    h.floor();
    Chicken c;
    c.pos = c.prevPos = glm::vec3(0.0f, 20.5f, 0.0f);
    h.run(c, 300);
    EXPECT_TRUE(c.onGround);
    EXPECT_FLOAT_EQ(c.pos.y, 0.5f);
    EXPECT_FLOAT_EQ(c.health, c.maxHealth);
    EXPECT_FALSE(c.dying);
}

TEST(ChickenTest, WingsFlapInAirAndSettleOnGround) {
    MobHarness h;
    h.floor();
    Chicken c;
    c.pos = c.prevPos = glm::vec3(0.0f, 10.5f, 0.0f);
    EXPECT_FLOAT_EQ(c.wingAngle(1.0f), 0.0f);

    h.run(c, 5);
    ASSERT_FALSE(c.onGround);
    EXPECT_FLOAT_EQ(c.flapSpeed, 1.0f) << "amplitude ramps to full within a few airborne ticks";
    float flapBefore = c.flap;
    h.run(c, 5);
    EXPECT_GT(c.flap - flapBefore, 5.0f) << "wing phase keeps advancing while airborne";
    float maxAngle = 0.0f;
    for (int i = 0; i < 10; ++i) {
        c.tick(h.ctx);
        maxAngle = std::max(maxAngle, c.wingAngle(1.0f));
    }
    EXPECT_GT(maxAngle, 1.0f) << "wings visibly open mid-flap";

    h.run(c, 200); // ~10 blocks at ~0.11 blocks/tick → lands, then rests
    ASSERT_TRUE(c.onGround);
    EXPECT_FLOAT_EQ(c.flapSpeed, 0.0f);
    EXPECT_FLOAT_EQ(c.wingAngle(0.5f), 0.0f) << "wings fold once grounded";
    EXPECT_LT(c.flapping, 0.01f);
}

TEST(ChickenTest, FloatsInWater) {
    MobHarness h;
    h.floor();
    h.blocks.fill(-3, 1, -3, 3, 6, 3, WATER); // water surface at y = 6.5
    Chicken c;
    c.pos = c.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    h.run(c, 100);
    EXPECT_TRUE(c.inWater);
    EXPECT_GT(c.pos.y, 4.5f);
    EXPECT_LT(c.pos.y, 7.0f);
}

TEST(ChickenTest, LaysEggWithinWindow) {
    for (unsigned seed : {1u, 42u, 999u}) {
        MobHarness h;
        h.rng.seed(seed);
        h.floor(40);
        std::vector<int> eggTicks;
        std::vector<ProjectileSpec> eggs;
        int tick = 0;
        h.ctx.onSpawnProjectile = [&](const ProjectileSpec& s) {
            eggTicks.push_back(tick);
            eggs.push_back(s);
        };
        Chicken c;
        c.id = 7;
        c.pos = c.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
        for (tick = 1; tick <= Chicken::EGG_MAX_TICKS; ++tick) c.tick(h.ctx);

        ASSERT_EQ(eggTicks.size(), 1u) << "seed " << seed;
        EXPECT_GE(eggTicks[0], Chicken::EGG_MIN_TICKS);
        EXPECT_LE(eggTicks[0], Chicken::EGG_MAX_TICKS);
        EXPECT_EQ(c.eggsLaid, 1);
        EXPECT_GT(c.eggTime, 0) << "timer re-armed after laying";
        // Harmless and owned by the chicken.
        EXPECT_FLOAT_EQ(eggs[0].damage, 0.0f);
        EXPECT_FALSE(eggs[0].hurtsPlayer);
        EXPECT_FALSE(eggs[0].hurtsMobs);
        EXPECT_EQ(eggs[0].shooterId, 7u);
    }
}

TEST(ChickenTest, DeadChickenLaysNoEggs) {
    MobHarness h;
    h.floor();
    int eggs = 0;
    h.ctx.onSpawnProjectile = [&](const ProjectileSpec&) { ++eggs; };
    Chicken c;
    c.pos = c.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    c.eggTime = 1;
    c.kill();
    h.run(c, 5);
    EXPECT_EQ(eggs, 0);
}
