#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "item.h"
#include "item_mesh.h"
#include "melee.h"

TEST(ItemTest, BlockItemsKeepTheirBlockValue) {
    // Old level.dat hotbars store block_type ints; they must still load.
    EXPECT_EQ(blockItem(STONE), static_cast<ItemId>(STONE));
    EXPECT_TRUE(isBlockItem(blockItem(TNT)));
    EXPECT_EQ(itemBlock(blockItem(WOOD)), WOOD);
}

TEST(ItemTest, SwordIsAToolThatPlacesNothing) {
    EXPECT_FALSE(isBlockItem(ITEM_IRON_SWORD));
    EXPECT_TRUE(isSword(ITEM_IRON_SWORD));
    EXPECT_EQ(itemBlock(ITEM_IRON_SWORD), AIR);
    EXPECT_FALSE(isSword(blockItem(STONE)));
    EXPECT_EQ(itemIconLayer(ITEM_IRON_SWORD), -1) << "tool icons are standalone images";
    EXPECT_NE(toolIconPath(ITEM_IRON_SWORD), nullptr);
    EXPECT_EQ(toolIconPath(blockItem(STONE)), nullptr);
}

TEST(ItemTest, ValidatesSavedIds) {
    EXPECT_TRUE(isValidItem(ITEM_EMPTY));
    EXPECT_TRUE(isValidItem(blockItem(POPPY)));
    EXPECT_TRUE(isValidItem(ITEM_IRON_SWORD));
    EXPECT_FALSE(isValidItem(static_cast<ItemId>(POPPY) + 1));
    EXPECT_FALSE(isValidItem(LAST_TOOL_ITEM + 1));
}

TEST(ItemTest, MinecraftMeleeStats) {
    EXPECT_FLOAT_EQ(meleeStats(ITEM_IRON_SWORD).damage, 6.0f);
    EXPECT_DOUBLE_EQ(meleeStats(ITEM_IRON_SWORD).cooldownSeconds, 0.625);
    EXPECT_FLOAT_EQ(meleeStats(ITEM_EMPTY).damage, 1.0f);
    EXPECT_DOUBLE_EQ(meleeStats(blockItem(DIRT)).cooldownSeconds, 0.25);
}

TEST(MeleeTest, AttackStrengthRampsOverTheCooldown) {
    EXPECT_FLOAT_EQ(melee::attackStrength(10.0, 0.625), 1.0f);
    EXPECT_NEAR(melee::attackStrength(0.0, 0.625), 0.04f, 1e-6f) << "half a tick of charge";
    EXPECT_NEAR(melee::attackStrength(0.3, 0.625), 0.52f, 1e-6f);
}

TEST(MeleeTest, DamageScalesWithChargeAndCritsNeedCharge) {
    EXPECT_FLOAT_EQ(melee::damage(6.0f, 1.0f, false), 6.0f);
    EXPECT_FLOAT_EQ(melee::damage(6.0f, 1.0f, true), 9.0f) << "iron sword crit";
    EXPECT_NEAR(melee::damage(6.0f, 0.0f, false), 1.2f, 1e-6f) << "spam-click floor is 20%";
    EXPECT_NEAR(melee::damage(6.0f, 0.5f, true), 6.0f * (0.2f + 0.25f * 0.8f), 1e-6f) << "no crit when weak";
}

// 3×2 sprite (top row first): X X .
//                                X . .
std::vector<uint8_t> tinySprite() {
    std::vector<uint8_t> px(3 * 2 * 4, 0);
    auto set = [&](int x, int y) {
        uint8_t* p = &px[(y * 3 + x) * 4];
        p[0] = 255;
        p[3] = 255;
    };
    set(0, 0);
    set(1, 0);
    set(0, 1);
    return px;
}

TEST(ItemMeshTest, ExtrudesOpaquePixelsWithEdgeFacesOnly) {
    auto px = tinySprite();
    std::vector<ItemMeshVertex> v = extrudeSprite(px.data(), 3, 2);
    // 3 pixels × (front + back) = 6 quads; exposed sides: the L-shape's
    // perimeter is 8 unit edges. 14 quads × 6 vertices.
    EXPECT_EQ(v.size(), 14u * 6u);
    for (const ItemMeshVertex& x : v) {
        EXPECT_FLOAT_EQ(x.color.r, 1.0f);
        EXPECT_GE(x.pos.z, -0.5f);
        EXPECT_LE(x.pos.z, 0.5f);
    }
}

TEST(ItemMeshTest, ImageTopRowIsHighestY) {
    auto px = tinySprite();
    float maxY = 0.0f;
    for (const ItemMeshVertex& x : extrudeSprite(px.data(), 3, 2)) maxY = std::max(maxY, x.pos.y);
    EXPECT_FLOAT_EQ(maxY, 2.0f);
}

TEST(ItemMeshTest, TransparentSpriteIsEmpty) {
    std::vector<uint8_t> px(16 * 16 * 4, 0);
    EXPECT_TRUE(extrudeSprite(px.data(), 16, 16).empty());
}
