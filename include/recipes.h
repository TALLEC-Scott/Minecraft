#pragma once

// Crafting recipes and the grid matcher, with Minecraft's rules: a shaped
// recipe matches anywhere in the grid (only the filled cells' bounding box
// counts), also mirrored left-right, and any recipe that fits in 2×2 can
// be made in the inventory grid; bigger ones need a crafting table. Pure,
// so it is unit-tested; the screens are in inventory.cpp.

#include <algorithm>
#include <array>
#include <vector>

#include "inventory_model.h"
#include "item.h"

struct Recipe {
    int width, height;            // pattern size (1..3)
    std::array<ItemId, 9> cells;  // row-major, width × height used; ITEM_EMPTY = must be empty
    ItemDrop result;
};

inline const std::vector<Recipe>& allRecipes() {
    constexpr ItemId _ = ITEM_EMPTY;
    constexpr ItemId S = ITEM_STICK;
    static const std::vector<Recipe> recipes = [] {
        std::vector<Recipe> r = {
            {1, 1, {blockItem(WOOD)}, {blockItem(PLANKS), 4}},
            {1, 2, {blockItem(PLANKS), blockItem(PLANKS)}, {ITEM_STICK, 4}},
            {2, 2, {blockItem(PLANKS), blockItem(PLANKS), blockItem(PLANKS), blockItem(PLANKS)},
             {blockItem(CRAFTING_TABLE), 1}},
        };
        struct Tier {
            ItemId material, pickaxe, axe, shovel, sword;
        };
        const Tier tiers[] = {
            {blockItem(PLANKS), ITEM_WOODEN_PICKAXE, ITEM_WOODEN_AXE, ITEM_WOODEN_SHOVEL, ITEM_WOODEN_SWORD},
            {blockItem(COBBLESTONE), ITEM_STONE_PICKAXE, ITEM_STONE_AXE, ITEM_STONE_SHOVEL, ITEM_STONE_SWORD},
        };
        for (const Tier& t : tiers) {
            const ItemId M = t.material;
            r.push_back({3, 3, {M, M, M, _, S, _, _, S, _}, {t.pickaxe, 1}});
            r.push_back({2, 3, {M, M, M, S, _, S}, {t.axe, 1}});
            r.push_back({1, 3, {M, S, S}, {t.shovel, 1}});
            r.push_back({1, 3, {M, M, S}, {t.sword, 1}});
        }
        return r;
    }();
    return recipes;
}

// What the `gridSize`×`gridSize` grid (row-major, `gridSize` 2 or 3)
// crafts; count 0 when nothing matches.
inline ItemDrop craftResult(const ItemId* grid, int gridSize) {
    int minX = gridSize, minY = gridSize, maxX = -1, maxY = -1;
    for (int y = 0; y < gridSize; y++)
        for (int x = 0; x < gridSize; x++)
            if (grid[y * gridSize + x] != ITEM_EMPTY) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
    if (maxX < 0) return {};
    const int w = maxX - minX + 1, h = maxY - minY + 1;
    auto at = [&](int x, int y) { return grid[(minY + y) * gridSize + minX + x]; };
    for (const Recipe& r : allRecipes()) {
        if (r.width != w || r.height != h) continue;
        for (bool mirrored : {false, true}) {
            bool match = true;
            for (int y = 0; y < h && match; y++)
                for (int x = 0; x < w && match; x++)
                    match = at(mirrored ? w - 1 - x : x, y) == r.cells[y * w + x];
            if (match) return r.result;
        }
    }
    return {};
}

// The crafting grid of a screen: 2×2 in the inventory, 3×3 at a table.
struct CraftingGrid {
    int size = 2;
    std::array<ItemStack, 9> cells{}; // row-major, size × size used

    ItemDrop result() const {
        ItemId ids[9] = {};
        for (int i = 0; i < size * size; i++) ids[i] = cells[i].empty() ? ITEM_EMPTY : cells[i].id;
        return craftResult(ids, size);
    }

    // Uses up one item from every filled cell.
    void consume() {
        for (int i = 0; i < size * size; i++) {
            if (cells[i].empty()) continue;
            if (--cells[i].count <= 0) cells[i].clear();
        }
    }

    // Click on the output: one craft onto the carried stack, if it fits.
    bool takeResult(ItemStack& cursor) {
        ItemDrop out = result();
        if (out.count <= 0) return false;
        if (!cursor.empty() && (cursor.id != out.id || cursor.count + out.count > maxStackSize(out.id))) return false;
        if (cursor.empty()) cursor = {out.id, 0};
        cursor.count += out.count;
        consume();
        return true;
    }

    // Shift-click on the output: craft as many as the inventory has room
    // for, while the grid keeps making the same thing.
    int craftAll(PlayerInventory& inv) {
        ItemDrop out = result();
        int crafted = 0;
        while (out.count > 0) {
            PlayerInventory trial = inv;
            if (trial.add(out.id, out.count) > 0) break; // no room for a whole craft
            inv = trial;
            consume();
            crafted++;
            ItemDrop next = result();
            if (next.id != out.id) break;
            out = next;
        }
        return crafted;
    }

    // Empties the grid into `inv`; returns what didn't fit, for dropping.
    std::vector<ItemStack> returnTo(PlayerInventory& inv) {
        std::vector<ItemStack> leftovers;
        for (ItemStack& c : cells) {
            if (c.empty()) continue;
            int left = inv.add(c.id, c.count);
            if (left > 0) leftovers.push_back({c.id, left});
            c.clear();
        }
        return leftovers;
    }
};
