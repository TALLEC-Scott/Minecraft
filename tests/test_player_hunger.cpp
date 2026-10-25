#include <gtest/gtest.h>
#include "player_hunger.h"

TEST(PlayerHungerTest, StartsFull) {
    PlayerHunger h;
    EXPECT_EQ(h.food, 20);
    EXPECT_FLOAT_EQ(h.saturation, 5.0f);
    EXPECT_FALSE(h.canEat());
    EXPECT_TRUE(h.canSprint());
}

TEST(PlayerHungerTest, ExhaustionBurnsSaturationThenFood) {
    PlayerHunger h;
    h.saturation = 1.0f;
    h.addExhaustion(4.5f);
    h.tick(20.0f, 20.0f);
    EXPECT_FLOAT_EQ(h.saturation, 0.0f);
    EXPECT_EQ(h.food, 20);
    h.addExhaustion(4.5f);
    h.tick(20.0f, 20.0f);
    EXPECT_EQ(h.food, 19);
}

TEST(PlayerHungerTest, ExhaustionIsCapped) {
    PlayerHunger h;
    h.addExhaustion(1000.0f);
    EXPECT_FLOAT_EQ(h.exhaustion, PlayerHunger::MAX_EXHAUSTION);
}

TEST(PlayerHungerTest, EatingRestoresFoodAndSaturationUpToFood) {
    PlayerHunger h;
    h.food = 10;
    h.saturation = 0.0f;
    h.eat(foodStats(ITEM_RAW_BEEF)); // 3 food, 1.8 saturation
    EXPECT_EQ(h.food, 13);
    EXPECT_FLOAT_EQ(h.saturation, 1.8f);
    h.food = 19;
    h.eat(foodStats(ITEM_APPLE));
    EXPECT_EQ(h.food, 20);
}

TEST(PlayerHungerTest, SaturatedFullBarHealsFast) {
    PlayerHunger h; // food 20, saturation 5
    float healed = 0.0f;
    for (int i = 0; i < 10; i++) healed += h.tick(10.0f, 20.0f);
    EXPECT_NEAR(healed, 5.0f / 6.0f, 1e-5f); // all 5 saturation spent at once (capped at 6)
    EXPECT_GT(h.exhaustion, 4.0f);
}

TEST(PlayerHungerTest, SlowRegenAtEighteenFood) {
    PlayerHunger h;
    h.food = 18;
    h.saturation = 0.0f;
    float healed = 0.0f;
    for (int i = 0; i < 79; i++) healed += h.tick(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(healed, 0.0f);
    healed += h.tick(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(healed, 1.0f); // 1 HP every 4 s
}

TEST(PlayerHungerTest, NoRegenBelowEighteen) {
    PlayerHunger h;
    h.food = 17;
    h.saturation = 0.0f;
    float healed = 0.0f;
    for (int i = 0; i < 400; i++) healed += h.tick(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(healed, 0.0f);
}

TEST(PlayerHungerTest, StarvationStopsAtOneHealth) {
    PlayerHunger h;
    h.food = 0;
    h.saturation = 0.0f;
    float change = 0.0f;
    for (int i = 0; i < 80; i++) change += h.tick(5.0f, 20.0f);
    EXPECT_FLOAT_EQ(change, -1.0f);
    change = 0.0f;
    for (int i = 0; i < 400; i++) change += h.tick(1.0f, 20.0f);
    EXPECT_FLOAT_EQ(change, 0.0f);
}

TEST(PlayerHungerTest, SprintNeedsMoreThanSixFood) {
    PlayerHunger h;
    h.food = 6;
    EXPECT_FALSE(h.canSprint());
    h.food = 7;
    EXPECT_TRUE(h.canSprint());
}
