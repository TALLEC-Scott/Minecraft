#include <gtest/gtest.h>
#include "level_data.h"

#include <sstream>

static LevelData roundTrip(const LevelData& in) {
    std::stringstream ss;
    writeLevelData(ss, in);
    LevelData out;
    readLevelData(ss, out);
    return out;
}

TEST(LevelDataTest, RoundTrip) {
    LevelData in;
    in.seed = 42;
    in.worldName = "My World";
    in.lastPlayed = 1700000000;
    in.player.position = glm::vec3(1.5f, 70.0f, -3.25f);
    in.player.yaw = 12.0f;
    in.player.pitch = -30.0f;
    in.player.walkMode = true;
    in.player.selectedSlot = 3;
    in.player.hotbar[0] = ITEM_IRON_SWORD;
    in.player.gameMode = GameMode::Survival;

    LevelData out = roundTrip(in);
    EXPECT_EQ(out.seed, 42u);
    EXPECT_EQ(out.worldName, "My World");
    EXPECT_EQ(out.lastPlayed, in.lastPlayed);
    EXPECT_FLOAT_EQ(out.player.position.x, 1.5f);
    EXPECT_FLOAT_EQ(out.player.position.z, -3.25f);
    EXPECT_FLOAT_EQ(out.player.pitch, -30.0f);
    EXPECT_TRUE(out.player.walkMode);
    EXPECT_EQ(out.player.selectedSlot, 3);
    EXPECT_EQ(out.player.hotbar[0], ITEM_IRON_SWORD);
    EXPECT_EQ(out.player.gameMode, GameMode::Survival);
}

TEST(LevelDataTest, OldFileWithoutGameModeIsCreative) {
    std::istringstream old("seed=7\nposX=1\nposY=2\nposZ=3\nwalkMode=1\n");
    LevelData out;
    readLevelData(old, out);
    EXPECT_EQ(out.seed, 7u);
    EXPECT_EQ(out.player.gameMode, GameMode::Creative);
    EXPECT_TRUE(out.player.walkMode);
}

TEST(LevelDataTest, MalformedValuesKeepDefaults) {
    std::istringstream bad("seed=abc\nposX=oops\ngameMode=\nselectedSlot=99\nhotbar=1,x,99999\nunknown=5\n");
    LevelData out;
    readLevelData(bad, out);
    EXPECT_EQ(out.seed, 0u);
    EXPECT_FLOAT_EQ(out.player.position.x, PlayerSaveData{}.position.x);
    EXPECT_EQ(out.player.gameMode, GameMode::Creative);
    EXPECT_EQ(out.player.selectedSlot, SAVED_HOTBAR_SIZE - 1);
    EXPECT_EQ(out.player.hotbar[0], 1);
}
