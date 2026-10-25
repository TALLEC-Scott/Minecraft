#include <gtest/gtest.h>
#include "inventory_model.h"

// PlayerInventory + clickSlot (the data model behind the E screen; the
// screen itself needs GL).

TEST(InventoryModelTest, StackSizes) {
    EXPECT_EQ(maxStackSize(DIRT), 64);
    EXPECT_EQ(maxStackSize(ITEM_IRON_SWORD), 1);
}

TEST(InventoryModelTest, AddFillsHotbarFirst) {
    PlayerInventory inv;
    EXPECT_EQ(inv.add(DIRT, 10), 0);
    EXPECT_EQ(inv.slots[0].id, DIRT);
    EXPECT_EQ(inv.slots[0].count, 10);
}

TEST(InventoryModelTest, AddTopsUpMatchingStacksBeforeEmptySlots) {
    PlayerInventory inv;
    inv.slots[3] = {STONE, 60};
    EXPECT_EQ(inv.add(STONE, 10), 0);
    EXPECT_EQ(inv.slots[3].count, 64);
    EXPECT_EQ(inv.slots[0].id, STONE); // the 6 that didn't fit went to the first empty slot
    EXPECT_EQ(inv.slots[0].count, 6);
}

TEST(InventoryModelTest, AddSpillsIntoStorageAndReportsLeftover) {
    PlayerInventory inv;
    for (auto& s : inv.slots) s = {SAND, 64};
    inv.slots[PlayerInventory::SIZE - 1].clear();
    EXPECT_EQ(inv.add(DIRT, 100), 36); // one free slot takes 64
    EXPECT_EQ(inv.slots[PlayerInventory::SIZE - 1].count, 64);
}

TEST(InventoryModelTest, ToolsDontStack) {
    PlayerInventory inv;
    EXPECT_EQ(inv.add(ITEM_IRON_SWORD, 2), 0);
    EXPECT_EQ(inv.slots[0].count, 1);
    EXPECT_EQ(inv.slots[1].count, 1);
}

TEST(InventoryModelTest, TakeClearsEmptiedSlot) {
    PlayerInventory inv;
    inv.slots[2] = {WOOD, 2};
    inv.take(2, 1);
    EXPECT_EQ(inv.slots[2].count, 1);
    inv.take(2, 5);
    EXPECT_TRUE(inv.slots[2].empty());
    EXPECT_EQ(inv.slots[2].id, ITEM_EMPTY);
}

TEST(InventoryModelTest, CountOf) {
    PlayerInventory inv;
    inv.slots[0] = {DIRT, 5};
    inv.slots[20] = {DIRT, 7};
    inv.slots[4] = {SAND, 3};
    EXPECT_EQ(inv.countOf(DIRT), 12);
    EXPECT_EQ(inv.countOf(STONE), 0);
}

TEST(InventoryModelTest, QuickMoveHotbarToStorageAndBack) {
    PlayerInventory inv;
    inv.slots[0] = {DIRT, 10};
    inv.slots[15] = {DIRT, 60};
    inv.quickMove(0);
    EXPECT_EQ(inv.slots[15].count, 64); // topped up first
    EXPECT_EQ(inv.slots[10].count, 6);  // rest into the first empty storage slot
    EXPECT_TRUE(inv.slots[0].empty());

    inv.quickMove(15);
    EXPECT_EQ(inv.slots[0].id, DIRT);
    EXPECT_EQ(inv.slots[0].count, 64);
}

TEST(InventoryModelTest, DefaultCreativeInventoryIsTheOldHotbar) {
    PlayerInventory inv = defaultCreativeInventory();
    EXPECT_TRUE(inv.slots[0].empty());
    EXPECT_EQ(inv.slots[1].id, GRASS);
    EXPECT_EQ(inv.slots[1].count, 1);
    EXPECT_EQ(inv.slots[9].id, SNOW);
    EXPECT_TRUE(inv.slots[PlayerInventory::HOTBAR_SIZE].empty());
}

// --- clickSlot (Minecraft inventory click rules) ---

TEST(InventoryClickTest, LeftPicksUpWholeStack) {
    ItemStack slot{DIRT, 10}, cursor;
    clickSlot(slot, cursor, false);
    EXPECT_TRUE(slot.empty());
    EXPECT_EQ(cursor.id, DIRT);
    EXPECT_EQ(cursor.count, 10);
}

TEST(InventoryClickTest, RightPicksUpHalfRoundedUp) {
    ItemStack slot{DIRT, 7}, cursor;
    clickSlot(slot, cursor, true);
    EXPECT_EQ(cursor.count, 4);
    EXPECT_EQ(slot.count, 3);
}

TEST(InventoryClickTest, LeftPutsDownIntoEmptySlot) {
    ItemStack slot, cursor{STONE, 5};
    clickSlot(slot, cursor, false);
    EXPECT_EQ(slot.id, STONE);
    EXPECT_EQ(slot.count, 5);
    EXPECT_TRUE(cursor.empty());
}

TEST(InventoryClickTest, RightPutsDownOne) {
    ItemStack slot, cursor{STONE, 5};
    clickSlot(slot, cursor, true);
    EXPECT_EQ(slot.count, 1);
    EXPECT_EQ(cursor.count, 4);
    clickSlot(slot, cursor, true);
    EXPECT_EQ(slot.count, 2);
    EXPECT_EQ(cursor.count, 3);
}

TEST(InventoryClickTest, LeftMergesUpToMax) {
    ItemStack slot{SAND, 60}, cursor{SAND, 10};
    clickSlot(slot, cursor, false);
    EXPECT_EQ(slot.count, 64);
    EXPECT_EQ(cursor.count, 6);
}

TEST(InventoryClickTest, DifferentItemsSwap) {
    ItemStack slot{SAND, 3}, cursor{DIRT, 9};
    clickSlot(slot, cursor, false);
    EXPECT_EQ(slot.id, DIRT);
    EXPECT_EQ(slot.count, 9);
    EXPECT_EQ(cursor.id, SAND);
    EXPECT_EQ(cursor.count, 3);
}

TEST(InventoryClickTest, RightOnFullSameStackDoesNothing) {
    ItemStack slot{SAND, 64}, cursor{SAND, 2};
    clickSlot(slot, cursor, true);
    EXPECT_EQ(slot.count, 64);
    EXPECT_EQ(cursor.count, 2);
}

TEST(InventoryClickTest, EmptyClickDoesNothing) {
    ItemStack slot, cursor;
    clickSlot(slot, cursor, false);
    EXPECT_TRUE(slot.empty());
    EXPECT_TRUE(cursor.empty());
}
