#include <gtest/gtest.h>

#include "player_health.h"

TEST(PlayerHealthTest, StartsFull) {
    PlayerHealth h;
    EXPECT_FLOAT_EQ(h.health, 20.0f);
    EXPECT_FALSE(h.isDead());
    EXPECT_FLOAT_EQ(h.hurtFlash(0.0), 0.0f);
}

TEST(PlayerHealthTest, InvulnerabilityWindow) {
    PlayerHealth h;
    EXPECT_TRUE(h.hurt(5.0f, 10.0));
    EXPECT_FLOAT_EQ(h.health, 15.0f);
    EXPECT_FALSE(h.hurt(5.0f, 10.2)) << "inside the 0.5 s window";
    EXPECT_FLOAT_EQ(h.health, 15.0f);
    EXPECT_TRUE(h.hurt(5.0f, 10.6));
    EXPECT_FLOAT_EQ(h.health, 10.0f);
}

TEST(PlayerHealthTest, HurtFlashFades) {
    PlayerHealth h;
    h.hurt(1.0f, 3.0);
    EXPECT_FLOAT_EQ(h.hurtFlash(3.0), 1.0f);
    EXPECT_GT(h.hurtFlash(3.2), 0.0f);
    EXPECT_FLOAT_EQ(h.hurtFlash(3.6), 0.0f);
}

TEST(PlayerHealthTest, RegeneratesOnlyAfterQuietPeriod) {
    PlayerHealth h;
    h.hurt(4.0f, 0.0);
    double t = 0.0;
    for (; t < PlayerHealth::REGEN_DELAY - 0.1; t += 0.1) h.update(t);
    EXPECT_FLOAT_EQ(h.health, 16.0f) << "no regen right after a hit";
    for (; t < 30.0; t += 0.1) h.update(t);
    EXPECT_GT(h.health, 16.0f);
    EXPECT_LE(h.health, 20.0f);
    // ~1 HP / 4 s once regen starts: by 30 s, at most 7 HP back.
    EXPECT_LE(h.health, 16.0f + 7.0f);
    for (; t < 200.0; t += 0.1) h.update(t);
    EXPECT_FLOAT_EQ(h.health, PlayerHealth::MAX_HEALTH) << "caps at max";
}

TEST(PlayerHealthTest, DeathAndRespawn) {
    PlayerHealth h;
    EXPECT_TRUE(h.hurt(25.0f, 1.0));
    EXPECT_TRUE(h.isDead());
    EXPECT_FLOAT_EQ(h.health, 0.0f);
    EXPECT_FALSE(h.hurt(1.0f, 5.0)) << "can't hurt the dead";
    h.update(100.0);
    EXPECT_TRUE(h.isDead()) << "no regen while dead";
    h.respawn(101.0);
    EXPECT_FALSE(h.isDead());
    EXPECT_FLOAT_EQ(h.health, 20.0f);
}

TEST(PlayerHealthTest, FallDamageFormula) {
    EXPECT_FLOAT_EQ(PlayerHealth::fallDamage(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(PlayerHealth::fallDamage(3.0f), 0.0f);
    EXPECT_FLOAT_EQ(PlayerHealth::fallDamage(3.2f), 1.0f);
    EXPECT_FLOAT_EQ(PlayerHealth::fallDamage(10.0f), 7.0f);
    EXPECT_FLOAT_EQ(PlayerHealth::fallDamage(23.5f), 21.0f);
}
