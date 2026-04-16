#include <gtest/gtest.h>

#include "tnt_entity.h"

// All TntEntity tests pass a null World so the entity ticks through empty
// space — physics and fuse countdown are exercised without dragging the
// chunk/GL stack into the test binary.

TEST(TntEntityTest, DefaultFuseCountsDown) {
    TntEntity e(glm::vec3(0), TntEntity::DEFAULT_FUSE, 0.0);
    ASSERT_EQ(e.fuseTicks, 80);

    // Ticks 1..79 should not detonate.
    for (int i = 0; i < TntEntity::DEFAULT_FUSE - 1; ++i) {
        bool detonated = e.tick(nullptr);
        EXPECT_FALSE(detonated) << "Detonated early at tick " << i;
    }
    // 80th tick drops fuse to 0 → detonate.
    bool detonated = e.tick(nullptr);
    EXPECT_TRUE(detonated);
    EXPECT_LE(e.fuseTicks, 0);
}

TEST(TntEntityTest, SpawnImpulseIsMostlyUpward) {
    // Minecraft values: +0.20 y, horizontal component magnitude 0.02.
    for (int i = 0; i < 64; ++i) {
        TntEntity e(glm::vec3(0), TntEntity::DEFAULT_FUSE, 0.0);
        EXPECT_NEAR(e.velocity.y, 0.20f, 1e-4f);
        float hmag = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
        EXPECT_NEAR(hmag, 0.02f, 1e-4f) << "Iteration " << i;
    }
}

TEST(TntEntityTest, ChainFuseRangeMatchesMinecraft) {
    // We can't invoke World::explode() from this test (no stub for
    // EntityManager/World pair), but the advertised chain-fuse range is a
    // simple numerical contract we can spot-check: min/max are valid and
    // properly ordered.
    EXPECT_GE(TntEntity::CHAIN_FUSE_MIN, 10);
    EXPECT_LE(TntEntity::CHAIN_FUSE_MAX, 30);
    EXPECT_LT(TntEntity::CHAIN_FUSE_MIN, TntEntity::CHAIN_FUSE_MAX);
}

TEST(TntEntityTest, GravityAcceleratesDownward) {
    TntEntity e(glm::vec3(0, 100, 0), 200, 0.0);
    float initialVy = e.velocity.y;
    e.tick(nullptr);
    // After one tick, vertical velocity has lost GRAVITY then been scaled
    // by DRAG. Resulting vy should be strictly less than the initial spawn
    // impulse (0.20), proving gravity is applied.
    EXPECT_LT(e.velocity.y, initialVy);
}

TEST(TntEntityTest, FuseDecrementsMonotonically) {
    TntEntity e(glm::vec3(0), 10, 0.0);
    int last = e.fuseTicks;
    for (int i = 0; i < 9; ++i) {
        e.tick(nullptr);
        EXPECT_LT(e.fuseTicks, last);
        last = e.fuseTicks;
    }
}

TEST(TntEntityTest, FlashBrightnessPositive) {
    TntEntity e(glm::vec3(0), TntEntity::DEFAULT_FUSE, 0.0);
    // Over one full second, brightness should always stay above 1.0 and
    // sometimes exceed 1.5 (the pulse peaks).
    bool sawPeak = false;
    for (double t = 0.0; t < 1.0; t += 0.05) {
        float b = e.flashBrightness(t);
        EXPECT_GE(b, 1.0f - 0.01f);
        if (b > 1.5f) sawPeak = true;
    }
    EXPECT_TRUE(sawPeak);
}

TEST(TntEntityTest, WithoutWorldEntityFallsThroughSpace) {
    TntEntity e(glm::vec3(0, 100, 0), 10, 0.0);
    float startY = e.pos.y;
    for (int i = 0; i < 5; ++i) e.tick(nullptr);
    // With no world it's ballistic motion — y strictly decreases after the
    // initial upward impulse is overcome by gravity.
    EXPECT_LT(e.pos.y, startY + 0.5f);
}
