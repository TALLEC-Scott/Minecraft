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
    in.player.inventory.clear();
    in.player.inventory.slots[0] = {ITEM_IRON_SWORD, 1};
    in.player.inventory.slots[25] = {DIRT, 42};
    in.player.gameMode = GameMode::Survival;
    in.player.health = 7.5f;
    in.player.food = 12;
    in.player.saturation = 2.25f;

    LevelData out = roundTrip(in);
    EXPECT_EQ(out.seed, 42u);
    EXPECT_EQ(out.worldName, "My World");
    EXPECT_EQ(out.lastPlayed, in.lastPlayed);
    EXPECT_FLOAT_EQ(out.player.position.x, 1.5f);
    EXPECT_FLOAT_EQ(out.player.position.z, -3.25f);
    EXPECT_FLOAT_EQ(out.player.pitch, -30.0f);
    EXPECT_TRUE(out.player.walkMode);
    EXPECT_EQ(out.player.selectedSlot, 3);
    EXPECT_EQ(out.player.inventory.slots[0].id, ITEM_IRON_SWORD);
    EXPECT_EQ(out.player.inventory.slots[25].id, DIRT);
    EXPECT_EQ(out.player.inventory.slots[25].count, 42);
    EXPECT_TRUE(out.player.inventory.slots[1].empty());
    EXPECT_EQ(out.player.gameMode, GameMode::Survival);
    EXPECT_FLOAT_EQ(out.player.health, 7.5f);
    EXPECT_EQ(out.player.food, 12);
    EXPECT_FLOAT_EQ(out.player.saturation, 2.25f);
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
    EXPECT_EQ(out.player.selectedSlot, PlayerInventory::HOTBAR_SIZE - 1);
    EXPECT_EQ(out.player.inventory.slots[0].id, 1);
}

TEST(LevelDataTest, PreStackHotbarLoadsOneOfEach) {
    std::istringstream old("hotbar=0,1,2,3,9,7,6,8,10,11\n");
    LevelData out;
    readLevelData(old, out);
    EXPECT_TRUE(out.player.inventory.slots[0].empty());
    EXPECT_EQ(out.player.inventory.slots[4].id, WOOD);
    EXPECT_EQ(out.player.inventory.slots[4].count, 1);
}

TEST(LevelDataTest, InventoryClampsCountsAndDropsBadIds) {
    std::istringstream f("inventory=3:200,9999:5,256:4,x:1,2:7\n");
    LevelData out;
    readLevelData(f, out);
    EXPECT_EQ(out.player.inventory.slots[0].count, 64);
    EXPECT_TRUE(out.player.inventory.slots[1].empty());
    EXPECT_EQ(out.player.inventory.slots[2].id, ITEM_IRON_SWORD);
    EXPECT_EQ(out.player.inventory.slots[2].count, 1);
    EXPECT_TRUE(out.player.inventory.slots[3].empty()); // "x:1"
    EXPECT_EQ(out.player.inventory.slots[4].count, 7);  // still read after the bad entry
}
