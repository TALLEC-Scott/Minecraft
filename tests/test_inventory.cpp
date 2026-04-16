#include <gtest/gtest.h>
#include "cube.h"

// Test the inventory's block list and hotbar slot logic.
// The actual Inventory class depends on GL (UIRenderer, TextureArray),
// so we test the underlying data model directly.

static constexpr int HOTBAR_SIZE = 10;

// Mirror of Inventory::PLACEABLE_BLOCKS — all non-AIR block types
static constexpr block_type PLACEABLE_BLOCKS[] = {
    GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE, WOOD, LEAVES, SNOW, GRAVEL, CACTUS, TNT,
};
static constexpr int NUM_PLACEABLE = sizeof(PLACEABLE_BLOCKS) / sizeof(PLACEABLE_BLOCKS[0]);

TEST(InventoryTest, AllBlockTypesPresent) {
    // Every non-AIR block_type should appear in the placeable list
    block_type allTypes[] = {GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND,
                             GLOWSTONE, WOOD, LEAVES, SNOW, GRAVEL, CACTUS, TNT};
    for (auto t : allTypes) {
        bool found = false;
        for (int i = 0; i < NUM_PLACEABLE; i++) {
            if (PLACEABLE_BLOCKS[i] == t) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Block type " << (int)t << " missing from PLACEABLE_BLOCKS";
    }
}

TEST(InventoryTest, NoAirInPlaceable) {
    for (int i = 0; i < NUM_PLACEABLE; i++) {
        EXPECT_NE(PLACEABLE_BLOCKS[i], AIR) << "AIR should not be in PLACEABLE_BLOCKS at index " << i;
    }
}

TEST(InventoryTest, NoDuplicatesInPlaceable) {
    for (int i = 0; i < NUM_PLACEABLE; i++) {
        for (int j = i + 1; j < NUM_PLACEABLE; j++) {
            EXPECT_NE(PLACEABLE_BLOCKS[i], PLACEABLE_BLOCKS[j])
                << "Duplicate at indices " << i << " and " << j;
        }
    }
}

TEST(InventoryTest, PlaceableCount) {
    // 15 block types total (AIR through TNT), minus AIR = 14 placeable
    EXPECT_EQ(NUM_PLACEABLE, 14);
}

// Simulate hotbar slot assignment (same logic as Player::setHotbarSlot)
static void setHotbarSlot(block_type hotbar[], int slot, block_type type) {
    if (slot >= 0 && slot < HOTBAR_SIZE) hotbar[slot] = type;
}

TEST(InventoryTest, SetHotbarSlot) {
    block_type hotbar[HOTBAR_SIZE] = {AIR, GRASS, DIRT, STONE, WOOD, SAND, WATER, GLOWSTONE, LEAVES, SNOW};

    setHotbarSlot(hotbar, 0, COAL_ORE);
    EXPECT_EQ(hotbar[0], COAL_ORE);

    setHotbarSlot(hotbar, 9, CACTUS);
    EXPECT_EQ(hotbar[9], CACTUS);

    // Other slots unchanged
    EXPECT_EQ(hotbar[1], GRASS);
    EXPECT_EQ(hotbar[4], WOOD);
}

TEST(InventoryTest, SetHotbarSlotBoundsCheck) {
    block_type hotbar[HOTBAR_SIZE] = {};
    for (int i = 0; i < HOTBAR_SIZE; i++) hotbar[i] = AIR;

    // Out of bounds should not crash or modify anything
    setHotbarSlot(hotbar, -1, STONE);
    setHotbarSlot(hotbar, 10, STONE);
    setHotbarSlot(hotbar, 100, STONE);

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        EXPECT_EQ(hotbar[i], AIR) << "Slot " << i << " should still be AIR";
    }
}

TEST(InventoryTest, ReplaceEverySlot) {
    block_type hotbar[HOTBAR_SIZE] = {};
    for (int i = 0; i < HOTBAR_SIZE; i++) hotbar[i] = AIR;

    // Assign a different block to each slot
    for (int i = 0; i < HOTBAR_SIZE && i < NUM_PLACEABLE; i++) {
        setHotbarSlot(hotbar, i, PLACEABLE_BLOCKS[i]);
    }

    for (int i = 0; i < HOTBAR_SIZE && i < NUM_PLACEABLE; i++) {
        EXPECT_EQ(hotbar[i], PLACEABLE_BLOCKS[i]) << "Slot " << i;
    }
}
