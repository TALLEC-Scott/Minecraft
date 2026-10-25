#pragma once

// Player inventory: 10 hotbar slots (keys 1–0) followed by 30 storage
// slots (three rows as wide as the hotbar), each holding a stack of one
// item. Pure (no GL) so it is
// unit-tested; the screens that edit it live in inventory.cpp.

#include <algorithm>
#include <array>

#include "item.h"

struct ItemStack {
    ItemId id = ITEM_EMPTY;
    int count = 0;

    bool empty() const { return count <= 0 || id == ITEM_EMPTY; }
    void clear() {
        id = ITEM_EMPTY;
        count = 0;
    }
};

// Tools don't stack, as in Minecraft; everything else stacks to 64.
inline int maxStackSize(ItemId id) {
    return isSword(id) ? 1 : 64;
}

class PlayerInventory {
  public:
    static constexpr int HOTBAR_SIZE = 10;
    static constexpr int STORAGE_SIZE = 30;
    static constexpr int SIZE = HOTBAR_SIZE + STORAGE_SIZE; // hotbar = slots 0..9

    std::array<ItemStack, SIZE> slots{};

    // Adds up to `count` of `id`: tops up matching stacks first, then fills
    // empty slots, hotbar before storage in both passes. Returns how many
    // didn't fit.
    int add(ItemId id, int count) {
        if (id == ITEM_EMPTY || count <= 0) return 0;
        const int max = maxStackSize(id);
        for (ItemStack& s : slots) {
            if (count == 0) break;
            if (s.empty() || s.id != id || s.count >= max) continue;
            int moved = std::min(count, max - s.count);
            s.count += moved;
            count -= moved;
        }
        for (ItemStack& s : slots) {
            if (count == 0) break;
            if (!s.empty()) continue;
            s.id = id;
            s.count = std::min(count, max);
            count -= s.count;
        }
        return count;
    }

    // Shift-click: moves slot `i` between the hotbar and storage, topping up
    // matching stacks first, then filling empty slots. Whatever doesn't fit
    // stays put.
    void quickMove(int i) {
        if (i < 0 || i >= SIZE || slots[i].empty()) return;
        const bool fromHotbar = i < HOTBAR_SIZE;
        const int begin = fromHotbar ? HOTBAR_SIZE : 0;
        const int end = fromHotbar ? SIZE : HOTBAR_SIZE;
        ItemStack& src = slots[i];
        const int max = maxStackSize(src.id);
        for (int pass = 0; pass < 2 && !src.empty(); pass++) {
            for (int j = begin; j < end && !src.empty(); j++) {
                ItemStack& dst = slots[j];
                bool fits = pass == 0 ? (!dst.empty() && dst.id == src.id && dst.count < max) : dst.empty();
                if (!fits) continue;
                if (dst.empty()) dst = {src.id, 0};
                int moved = std::min(src.count, max - dst.count);
                dst.count += moved;
                src.count -= moved;
            }
        }
        if (src.count <= 0) src.clear();
    }

    // Removes up to `count` from slot `i`; an emptied slot is cleared.
    void take(int i, int count) {
        if (i < 0 || i >= SIZE) return;
        ItemStack& s = slots[i];
        s.count -= std::min(count, s.count);
        if (s.count <= 0) s.clear();
    }

    int countOf(ItemId id) const {
        int n = 0;
        for (const ItemStack& s : slots)
            if (!s.empty() && s.id == id) n += s.count;
        return n;
    }

    void clear() {
        for (ItemStack& s : slots) s.clear();
    }
};

// The hotbar Creative players had before inventories (and still start
// with), one of each. Survival worlds start empty.
inline PlayerInventory defaultCreativeInventory() {
    PlayerInventory inv;
    const ItemId hotbar[] = {AIR, GRASS, DIRT, STONE, WOOD, SAND, WATER, GLOWSTONE, LEAVES, SNOW};
    for (int i = 0; i < PlayerInventory::HOTBAR_SIZE; i++)
        if (hotbar[i] != ITEM_EMPTY) inv.slots[i] = {hotbar[i], 1};
    return inv;
}

// One mouse click on an inventory slot while `cursor` holds the stack being
// carried, with Minecraft's rules:
//   left:  empty hand picks up the stack; otherwise put the carried stack
//          down, merge into the same item as far as it fits, or swap.
//   right: empty hand picks up half (rounded up); otherwise put one item
//          down onto an empty slot or the same item, or swap.
inline void clickSlot(ItemStack& slot, ItemStack& cursor, bool rightButton) {
    if (cursor.empty()) {
        if (slot.empty()) return;
        cursor = slot;
        if (rightButton) {
            cursor.count = (slot.count + 1) / 2;
            slot.count -= cursor.count;
            if (slot.count <= 0) slot.clear();
        } else {
            slot.clear();
        }
        return;
    }
    const int max = maxStackSize(cursor.id);
    if (slot.empty() || slot.id == cursor.id) {
        if (slot.empty()) {
            slot.id = cursor.id;
            slot.count = 0;
        }
        int moved = std::min(rightButton ? 1 : cursor.count, max - slot.count);
        if (moved <= 0 && !rightButton) {
            std::swap(slot, cursor); // full stack of the same item: Minecraft swaps
            return;
        }
        slot.count += std::max(moved, 0);
        cursor.count -= std::max(moved, 0);
        if (cursor.count <= 0) cursor.clear();
        return;
    }
    std::swap(slot, cursor);
}
