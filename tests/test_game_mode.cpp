#include <gtest/gtest.h>
#include "game_mode.h"

TEST(GameModeTest, ParsesMinecraftSpellings) {
    EXPECT_EQ(parseGameMode("survival"), GameMode::Survival);
    EXPECT_EQ(parseGameMode("Survival"), GameMode::Survival);
    EXPECT_EQ(parseGameMode("s"), GameMode::Survival);
    EXPECT_EQ(parseGameMode("0"), GameMode::Survival);
    EXPECT_EQ(parseGameMode("CREATIVE"), GameMode::Creative);
    EXPECT_EQ(parseGameMode("c"), GameMode::Creative);
    EXPECT_EQ(parseGameMode("1"), GameMode::Creative);
}

TEST(GameModeTest, RejectsUnknown) {
    EXPECT_FALSE(parseGameMode("").has_value());
    EXPECT_FALSE(parseGameMode("adventure").has_value());
    EXPECT_FALSE(parseGameMode("2").has_value());
}

TEST(GameModeTest, Names) {
    EXPECT_STREQ(gameModeName(GameMode::Survival), "Survival");
    EXPECT_STREQ(gameModeName(GameMode::Creative), "Creative");
}

TEST(GameModeTest, SurvivalReachIsShorter) {
    EXPECT_FLOAT_EQ(blockReach(GameMode::Survival), 4.5f);
    EXPECT_FLOAT_EQ(blockReach(GameMode::Creative), 5.0f);
    EXPECT_FLOAT_EQ(attackReach(GameMode::Survival), 3.0f);
    EXPECT_FLOAT_EQ(attackReach(GameMode::Creative), 5.0f);
}
