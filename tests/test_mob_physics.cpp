#include <gtest/gtest.h>

#include "mob_test_support.h"

// Mob physics against a fake block world. Coordinates follow the game's
// block-centered convention: a floor block at y = 0 has its top face at
// y = 0.5, so a mob standing on it has pos.y == 0.5.

TEST(MobPhysicsTest, FallsAndLandsOnFloor) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 8.3f, 0.0f);
    h.run(m, 80);
    EXPECT_FLOAT_EQ(m.pos.y, 0.5f);
    EXPECT_TRUE(m.onGround);
    // Resting velocity is one tick of gravity (zeroed on contact, then the
    // post-move gravity step) — same as Minecraft's -0.0784 on ground.
    EXPECT_NEAR(m.velocity.y, -Mob::TICK_GRAVITY * Mob::AIR_DRAG, 1e-5f);
}

TEST(MobPhysicsTest, GravityMatchesMinecraftPerTick) {
    MobHarness h; // no floor → free fall (y stays well above world bottom)
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 100.0f, 0.0f);
    m.tick(h.ctx);
    // After one tick: v = (0 - 0.08) * 0.98.
    EXPECT_NEAR(m.velocity.y, -0.0784f, 1e-5f);
    m.tick(h.ctx);
    EXPECT_NEAR(m.velocity.y, (-0.0784f - 0.08f) * 0.98f, 1e-5f);
}

TEST(MobPhysicsTest, LongFallDealsFallDamage) {
    MobHarness h;
    h.floor();
    TestMob m;
    // Feet 9.8 blocks above the floor top → floor(9.8 - 3) = 6 damage.
    m.pos = m.prevPos = glm::vec3(0.0f, 10.3f, 0.0f);
    h.run(m, 80);
    EXPECT_FLOAT_EQ(m.health, m.maxHealth - 6.0f);
}

TEST(MobPhysicsTest, FallJustUnderFourBlocksIsHarmless) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 4.3f, 0.0f); // 3.8-block drop
    h.run(m, 60);
    EXPECT_FLOAT_EQ(m.health, m.maxHealth);
}

TEST(MobPhysicsTest, FourBlockFallCostsOne) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 4.5f, 0.0f); // exactly 4 blocks
    h.run(m, 60);
    EXPECT_FLOAT_EQ(m.health, m.maxHealth - 1.0f);
}

TEST(MobPhysicsTest, ShortFallIsHarmless) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 3.4f, 0.0f); // 2.9-block drop
    h.run(m, 40);
    EXPECT_FLOAT_EQ(m.health, m.maxHealth);
    EXPECT_FLOAT_EQ(m.pos.y, 0.5f);
}

TEST(MobPhysicsTest, WallStopsHorizontalMovement) {
    MobHarness h;
    h.floor();
    // Three-high wall at x = 3 → face at x = 2.5. Too tall to step up.
    h.blocks.fill(3, 1, -5, 3, 3, 5, STONE);
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    for (int i = 0; i < 120; ++i) {
        m.navigateTo(glm::vec3(10.0f, 0.5f, 0.0f), 0.15f, false);
        m.tick(h.ctx);
    }
    EXPECT_LE(m.pos.x + m.halfWidth, 2.5f + 1e-3f);
    EXPECT_GT(m.pos.x, 1.5f) << "mob should have walked up to the wall";
    EXPECT_TRUE(m.horizontalCollision || m.navStuckTicks() > 0);
}

TEST(MobPhysicsTest, StepsUpOneBlockLedge) {
    MobHarness h;
    h.floor();
    h.blocks.fill(3, 1, -5, 12, 1, 5, STONE); // one-high plateau from x = 2.5 on
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    for (int i = 0; i < 120; ++i) {
        if (!m.isNavigating() && m.pos.x < 7.0f) m.navigateTo(glm::vec3(8.0f, 1.5f, 0.0f), 0.12f, false);
        m.tick(h.ctx);
    }
    EXPECT_FLOAT_EQ(m.pos.y, 1.5f);
    EXPECT_GT(m.pos.x, 4.0f);
}

TEST(MobPhysicsTest, AvoidsDeepDrop) {
    MobHarness h;
    // High ledge (top at y = 5.5) for x <= 2, then a 5+ block drop.
    h.blocks.fill(-6, 5, -3, 2, 5, 3, STONE);
    h.blocks.fill(-6, 0, -3, 12, 0, 3, STONE);
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 5.5f, 0.0f);
    m.onGround = true;
    m.navigateTo(glm::vec3(10.0f, 0.5f, 0.0f), 0.12f, true);
    h.run(m, 100);
    EXPECT_FLOAT_EQ(m.pos.y, 5.5f) << "mob walked off the cliff";
    EXPECT_FALSE(m.isNavigating());
}

TEST(MobPhysicsTest, FloatsUpInWater) {
    MobHarness h;
    h.floor();
    h.blocks.fill(-3, 1, -3, 3, 6, 3, WATER); // water surface at y = 6.5
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    h.run(m, 100);
    EXPECT_TRUE(m.inWater);
    EXPECT_GT(m.pos.y, 4.5f) << "mob should bob near the surface";
    EXPECT_LT(m.pos.y, 7.0f);
}

TEST(MobPhysicsTest, KnockbackVelocityDecaysOnGround) {
    MobHarness h;
    h.floor();
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    h.run(m, 2); // settle onGround
    ASSERT_TRUE(m.onGround);
    m.damage(1.0f, glm::vec3(-1.0f, 0.5f, 0.0f));
    h.run(m, 60);
    EXPECT_GT(m.pos.x, 0.5f) << "pushed away from the source (+X)";
    EXPECT_LT(m.pos.x, 4.0f) << "knockback should be short";
    EXPECT_NEAR(m.velocity.x, 0.0f, 1e-3f);
    EXPECT_TRUE(m.onGround);
}

TEST(MobPhysicsTest, WalkAnimationTracksSpeed) {
    MobHarness h;
    h.floor(30);
    TestMob m;
    m.pos = m.prevPos = glm::vec3(0.0f, 0.5f, 0.0f);
    h.run(m, 5);
    EXPECT_NEAR(m.walkAnimSpeed, 0.0f, 1e-4f);
    for (int i = 0; i < 30; ++i) {
        m.navigateTo(glm::vec3(25.0f, 0.5f, 0.0f), 0.1f, false);
        m.tick(h.ctx);
    }
    EXPECT_GT(m.walkAnimSpeed, 0.3f);
    EXPECT_GT(m.walkAnimPos, 5.0f);
    // Body turned toward +X (yaw 0).
    EXPECT_NEAR(wrapDegrees(m.yaw), 0.0f, 1.0f);
}

TEST(MobPhysicsTest, AngleHelpers) {
    EXPECT_FLOAT_EQ(wrapDegrees(190.0f), -170.0f);
    EXPECT_FLOAT_EQ(wrapDegrees(-190.0f), 170.0f);
    EXPECT_FLOAT_EQ(approachAngle(170.0f, -170.0f, 5.0f), 175.0f); // shortest way round
    EXPECT_NEAR(yawFromDirection(0.0f, 1.0f), 90.0f, 1e-4f);
}

// --- Goal selector ---------------------------------------------------------

namespace {
struct CountingGoal : MobGoal {
    bool want = false;
    int starts = 0, stops = 0, ticks = 0;
    explicit CountingGoal(uint8_t ch) : MobGoal(ch) {}
    bool canStart(Mob&, MobContext&) override { return want; }
    void start(Mob&, MobContext&) override { ++starts; }
    void stop(Mob&, MobContext&) override { ++stops; }
    void tick(Mob&, MobContext&) override { ++ticks; }
};
} // namespace

TEST(MobGoalTest, HigherPriorityInterruptsSameChannel) {
    MobHarness h;
    TestMob m;
    auto low = std::make_unique<CountingGoal>(GOAL_MOVE);
    auto high = std::make_unique<CountingGoal>(GOAL_MOVE);
    auto look = std::make_unique<CountingGoal>(GOAL_LOOK);
    CountingGoal* lowP = low.get();
    CountingGoal* highP = high.get();
    CountingGoal* lookP = look.get();
    m.goals.add(5, std::move(low));
    m.goals.add(1, std::move(high));
    m.goals.add(6, std::move(look));

    lowP->want = true;
    lookP->want = true;
    m.goals.tick(m, h.ctx);
    EXPECT_EQ(lowP->starts, 1);
    EXPECT_EQ(lookP->starts, 1);

    highP->want = true;
    m.goals.tick(m, h.ctx);
    EXPECT_EQ(highP->starts, 1);
    EXPECT_EQ(lowP->stops, 1) << "low-priority MOVE goal should be interrupted";
    EXPECT_EQ(lookP->stops, 0) << "LOOK channel is independent";
    EXPECT_TRUE(m.goals.isRunning<CountingGoal>());

    // While the high goal runs, the low goal can't restart.
    m.goals.tick(m, h.ctx);
    EXPECT_EQ(lowP->starts, 1);
}
