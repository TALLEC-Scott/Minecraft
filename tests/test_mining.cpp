#include <gtest/gtest.h>
#include "mining.h"

// Break times in ticks (1/20 s), checked against Minecraft Java's values.

TEST(MiningTest, BareHandTimes) {
    EXPECT_EQ(breakTicks(DIRT, ITEM_EMPTY), 15);  // 0.75 s
    EXPECT_EQ(breakTicks(GRASS, ITEM_EMPTY), 18); // 0.9 s
    EXPECT_EQ(breakTicks(WOOD, ITEM_EMPTY), 60);  // 3 s
    EXPECT_EQ(breakTicks(STONE, ITEM_EMPTY), 150); // 7.5 s, no drop
    EXPECT_EQ(breakTicks(LEAVES, ITEM_EMPTY), 6);
}

TEST(MiningTest, RightToolIsFaster) {
    EXPECT_EQ(breakTicks(STONE, ITEM_WOODEN_PICKAXE), 23); // 1.15 s
    EXPECT_EQ(breakTicks(STONE, ITEM_STONE_PICKAXE), 12);  // 0.6 s
    EXPECT_EQ(breakTicks(WOOD, ITEM_WOODEN_AXE), 30);
    EXPECT_EQ(breakTicks(DIRT, ITEM_STONE_SHOVEL), 4);
    // The wrong tool counts as a bare hand.
    EXPECT_EQ(breakTicks(STONE, ITEM_WOODEN_AXE), breakTicks(STONE, ITEM_EMPTY));
    EXPECT_EQ(breakTicks(DIRT, ITEM_IRON_SWORD), breakTicks(DIRT, ITEM_EMPTY));
}

TEST(MiningTest, InstantAndUnbreakable) {
    EXPECT_EQ(breakTicks(TNT, ITEM_EMPTY), 0);
    EXPECT_EQ(breakTicks(TALL_GRASS, ITEM_EMPTY), 0);
    EXPECT_EQ(breakTicks(BEDROCK, ITEM_STONE_PICKAXE), -1);
    EXPECT_EQ(breakTicks(WATER, ITEM_EMPTY), -1);
    EXPECT_EQ(breakTicks(AIR, ITEM_EMPTY), -1);
}

TEST(MiningTest, HarvestNeedsAPickaxeForStone) {
    EXPECT_FALSE(canHarvest(STONE, ITEM_EMPTY));
    EXPECT_FALSE(canHarvest(COAL_ORE, ITEM_STONE_AXE));
    EXPECT_TRUE(canHarvest(STONE, ITEM_WOODEN_PICKAXE));
    EXPECT_TRUE(canHarvest(DIRT, ITEM_EMPTY));
}

TEST(MiningTest, Drops) {
    EXPECT_EQ(blockDrop(GRASS, ITEM_EMPTY, 0.5f).id, blockItem(DIRT));
    EXPECT_EQ(blockDrop(STONE, ITEM_WOODEN_PICKAXE, 0.5f).id, blockItem(COBBLESTONE));
    EXPECT_EQ(blockDrop(STONE, ITEM_EMPTY, 0.5f).count, 0);
    EXPECT_EQ(blockDrop(COAL_ORE, ITEM_STONE_PICKAXE, 0.5f).id, ITEM_COAL);
    EXPECT_EQ(blockDrop(WOOD, ITEM_EMPTY, 0.5f).id, blockItem(WOOD));
    EXPECT_EQ(blockDrop(CRAFTING_TABLE, ITEM_EMPTY, 0.5f).id, blockItem(CRAFTING_TABLE));
    EXPECT_EQ(blockDrop(TALL_GRASS, ITEM_EMPTY, 0.5f).count, 0);
    EXPECT_EQ(blockDrop(BEDROCK, ITEM_STONE_PICKAXE, 0.5f).count, 0);
}

TEST(MiningTest, LeavesSometimesDropAnApple) {
    EXPECT_EQ(blockDrop(LEAVES, ITEM_EMPTY, 0.001f).id, ITEM_APPLE);
    EXPECT_EQ(blockDrop(LEAVES, ITEM_EMPTY, 0.5f).count, 0);
}

TEST(MiningTest, EveryBlockHasARow) {
    for (int i = 0; i < BLOCK_TYPE_COUNT; i++) {
        BlockMining m = blockMining(static_cast<block_type>(i));
        if (m.needsTool) {
            EXPECT_NE(m.bestTool, ToolKind::None) << i;
        }
    }
}
