#include <gtest/gtest.h>
#include "recipes.h"

static constexpr ItemId _ = ITEM_EMPTY;
static constexpr ItemId P = blockItem(PLANKS);
static constexpr ItemId C = blockItem(COBBLESTONE);
static constexpr ItemId S = ITEM_STICK;

TEST(RecipeTest, LogToPlanksAnywhere) {
    ItemId grid[4] = {_, _, _, blockItem(WOOD)};
    ItemDrop r = craftResult(grid, 2);
    EXPECT_EQ(r.id, P);
    EXPECT_EQ(r.count, 4);
}

TEST(RecipeTest, SticksNeedVerticalPlanks) {
    ItemId vertical[4] = {_, P, _, P};
    EXPECT_EQ(craftResult(vertical, 2).id, ITEM_STICK);
    EXPECT_EQ(craftResult(vertical, 2).count, 4);
    ItemId horizontal[4] = {P, P, _, _};
    EXPECT_EQ(craftResult(horizontal, 2).count, 0);
}

TEST(RecipeTest, CraftingTableFromFourPlanks) {
    ItemId grid[4] = {P, P, P, P};
    EXPECT_EQ(craftResult(grid, 2).id, blockItem(CRAFTING_TABLE));
}

TEST(RecipeTest, PickaxesNeedTheTable) {
    ItemId wood[9] = {P, P, P, _, S, _, _, S, _};
    EXPECT_EQ(craftResult(wood, 3).id, ITEM_WOODEN_PICKAXE);
    ItemId stone[9] = {C, C, C, _, S, _, _, S, _};
    EXPECT_EQ(craftResult(stone, 3).id, ITEM_STONE_PICKAXE);
}

TEST(RecipeTest, AxeMatchesMirrored) {
    ItemId left[9] = {P, P, _, P, S, _, _, S, _};
    ItemId right[9] = {_, P, P, _, S, P, _, S, _};
    EXPECT_EQ(craftResult(left, 3).id, ITEM_WOODEN_AXE);
    EXPECT_EQ(craftResult(right, 3).id, ITEM_WOODEN_AXE);
}

TEST(RecipeTest, ShovelAndSwordInAnyColumn) {
    ItemId shovel[9] = {_, _, C, _, _, S, _, _, S};
    EXPECT_EQ(craftResult(shovel, 3).id, ITEM_STONE_SHOVEL);
    ItemId sword[9] = {P, _, _, P, _, _, S, _, _};
    EXPECT_EQ(craftResult(sword, 3).id, ITEM_WOODEN_SWORD);
}

TEST(RecipeTest, ExtraItemsBreakTheMatch) {
    ItemId grid[9] = {P, P, P, _, S, _, _, S, P};
    EXPECT_EQ(craftResult(grid, 3).count, 0);
    ItemId mixed[9] = {P, C, P, _, S, _, _, S, _};
    EXPECT_EQ(craftResult(mixed, 3).count, 0);
}

TEST(RecipeTest, EmptyGridCraftsNothing) {
    ItemId grid[9] = {};
    EXPECT_EQ(craftResult(grid, 3).count, 0);
}

// --- CraftingGrid (the screen's grid + output slot) ---

TEST(CraftingGridTest, TakeResultUsesOneOfEachInput) {
    CraftingGrid g;
    g.cells[0] = {blockItem(WOOD), 3};
    ItemStack cursor;
    EXPECT_TRUE(g.takeResult(cursor));
    EXPECT_EQ(cursor.id, P);
    EXPECT_EQ(cursor.count, 4);
    EXPECT_EQ(g.cells[0].count, 2);
    EXPECT_TRUE(g.takeResult(cursor));
    EXPECT_EQ(cursor.count, 8);
}

TEST(CraftingGridTest, TakeResultRefusesADifferentCarriedItem) {
    CraftingGrid g;
    g.cells[0] = {blockItem(WOOD), 1};
    ItemStack cursor{blockItem(DIRT), 1};
    EXPECT_FALSE(g.takeResult(cursor));
    EXPECT_EQ(g.cells[0].count, 1);
}

TEST(CraftingGridTest, TakeResultRefusesWhenTheStackWouldOverflow) {
    CraftingGrid g;
    g.cells[0] = {blockItem(WOOD), 1};
    ItemStack cursor{P, 62};
    EXPECT_FALSE(g.takeResult(cursor));
}

TEST(CraftingGridTest, CraftAllUsesEverythingThatFits) {
    CraftingGrid g;
    g.cells[1] = {blockItem(WOOD), 5};
    PlayerInventory inv;
    EXPECT_EQ(g.craftAll(inv), 5);
    EXPECT_EQ(inv.countOf(P), 20);
    EXPECT_TRUE(g.cells[1].empty());
}

TEST(CraftingGridTest, ThreeByThreeToolCraft) {
    CraftingGrid g;
    g.size = 3;
    g.cells[0] = {C, 1};
    g.cells[1] = {C, 1};
    g.cells[2] = {C, 1};
    g.cells[4] = {S, 2};
    g.cells[7] = {S, 2};
    ItemStack cursor;
    EXPECT_TRUE(g.takeResult(cursor));
    EXPECT_EQ(cursor.id, ITEM_STONE_PICKAXE);
    EXPECT_EQ(g.cells[4].count, 1);
    EXPECT_TRUE(g.cells[0].empty());
    EXPECT_EQ(g.result().count, 0); // sticks alone make nothing
}

TEST(CraftingGridTest, ReturnToInventoryReportsLeftovers) {
    CraftingGrid g;
    g.cells[0] = {P, 10};
    PlayerInventory inv;
    for (auto& s : inv.slots) s = {blockItem(DIRT), 64};
    auto left = g.returnTo(inv);
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].count, 10);
    EXPECT_TRUE(g.cells[0].empty());
}
