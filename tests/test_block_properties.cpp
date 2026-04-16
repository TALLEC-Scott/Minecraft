#include <gtest/gtest.h>
#include "cube.h"

TEST(BlockFlagTest, AirIsNotSolid) {
    EXPECT_FALSE(hasFlag(AIR, BF_SOLID));
    EXPECT_FALSE(hasFlag(AIR, BF_OPAQUE));
    EXPECT_TRUE(hasFlag(AIR, BF_TRANSPARENT));
    EXPECT_FALSE(hasFlag(AIR, BF_LIQUID));
}

TEST(BlockFlagTest, SolidBlocks) {
    block_type solids[] = {GRASS,  DIRT,   STONE,  COAL_ORE, BEDROCK, SAND, GLOWSTONE,
                           WOOD,   SNOW,   GRAVEL, CACTUS,   LEAVES,  TNT};
    for (auto bt : solids) {
        EXPECT_TRUE(hasFlag(bt, BF_SOLID)) << "block_type " << bt << " should be solid";
    }
}

TEST(BlockFlagTest, OpaqueBlocks) {
    block_type opaques[] = {GRASS, DIRT,      STONE, COAL_ORE, BEDROCK, SAND, GLOWSTONE,
                            WOOD,  SNOW,      GRAVEL, CACTUS,   TNT};
    for (auto bt : opaques) {
        EXPECT_TRUE(hasFlag(bt, BF_OPAQUE)) << "block_type " << bt << " should be opaque";
    }
}

TEST(BlockFlagTest, WaterIsLiquid) {
    EXPECT_TRUE(hasFlag(WATER, BF_LIQUID));
    EXPECT_TRUE(hasFlag(WATER, BF_TRANSPARENT));
    EXPECT_FALSE(hasFlag(WATER, BF_SOLID));
    EXPECT_FALSE(hasFlag(WATER, BF_OPAQUE));
}

TEST(BlockFlagTest, LeavesAreFiltering) {
    EXPECT_TRUE(hasFlag(LEAVES, BF_TRANSLUCENT));
    EXPECT_TRUE(hasFlag(LEAVES, BF_SOLID));
    EXPECT_FALSE(hasFlag(LEAVES, BF_OPAQUE));
    EXPECT_FALSE(hasFlag(LEAVES, BF_LIQUID));
}

TEST(BlockFlagTest, NoBlockHasConflictingFlags) {
    // A block shouldn't be both opaque and transparent
    for (int i = 0; i <= TNT; i++) {
        auto bt = static_cast<block_type>(i);
        if (hasFlag(bt, BF_OPAQUE)) {
            EXPECT_FALSE(hasFlag(bt, BF_TRANSPARENT)) << "block_type " << i << " is both opaque and transparent";
        }
        // Liquid blocks shouldn't be solid
        if (hasFlag(bt, BF_LIQUID)) {
            EXPECT_FALSE(hasFlag(bt, BF_SOLID)) << "block_type " << i << " is both liquid and solid";
        }
    }
}

TEST(BlockFlagTest, MultipleFlagQuery) {
    // hasFlag with combined flags checks if ANY are set
    EXPECT_TRUE(hasFlag(DIRT, BF_SOLID | BF_OPAQUE));
    EXPECT_TRUE(hasFlag(WATER, BF_TRANSPARENT | BF_SOLID)); // transparent is set
    EXPECT_FALSE(hasFlag(AIR, BF_SOLID | BF_OPAQUE));        // neither set
}

TEST(BlockFlagTest, AllBlockTypesCovered) {
    // Ensure getBlockFlags doesn't crash for any valid block type
    for (int i = 0; i <= TNT; i++) {
        auto bt = static_cast<block_type>(i);
        uint32_t flags = getBlockFlags(bt);
        // Every block should have at least one property
        if (bt == AIR)
            EXPECT_TRUE(flags & BF_TRANSPARENT);
        else
            EXPECT_TRUE(flags != BF_NONE) << "block_type " << i << " has no flags";
    }
}

TEST(BlockFlagTest, OutOfRangeReturnsNone) {
    // Negative and past-the-end values should return BF_NONE
    EXPECT_EQ(getBlockFlags(static_cast<block_type>(-1)), BF_NONE);
    EXPECT_EQ(getBlockFlags(static_cast<block_type>(TNT + 1)), BF_NONE);
    EXPECT_EQ(getBlockFlags(static_cast<block_type>(999)), BF_NONE);
}

TEST(BlockFlagTest, HasFlagOutOfRangeIsFalse) {
    auto invalid = static_cast<block_type>(999);
    EXPECT_FALSE(hasFlag(invalid, BF_SOLID));
    EXPECT_FALSE(hasFlag(invalid, BF_OPAQUE));
    EXPECT_FALSE(hasFlag(invalid, BF_LIQUID));
}

TEST(BlockFlagTest, FirstAndLastBlockType) {
    // Boundary: first enum value
    EXPECT_EQ(getBlockFlags(AIR), BF_TRANSPARENT);
    // Boundary: last enum value
    EXPECT_TRUE(hasFlag(TNT, BF_SOLID));
    EXPECT_TRUE(hasFlag(TNT, BF_OPAQUE));
}

TEST(BlockFlagTest, ZeroFlagQueryAlwaysFalse) {
    EXPECT_FALSE(hasFlag(DIRT, BF_NONE));
    EXPECT_FALSE(hasFlag(AIR, BF_NONE));
}
