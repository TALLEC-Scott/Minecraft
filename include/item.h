#pragma once

// What a hotbar slot holds: a placeable block or a tool. Block items reuse
// their block_type value (0..255), so hotbars saved in level.dat before
// tools existed load unchanged; tools start at FIRST_TOOL_ITEM.

#include <cstdint>

#include "block_layers.h"
#include "cube.h"

using ItemId = uint16_t;

inline constexpr ItemId ITEM_EMPTY = AIR;
inline constexpr ItemId FIRST_TOOL_ITEM = 256;
inline constexpr ItemId ITEM_IRON_SWORD = 256;
inline constexpr ItemId LAST_TOOL_ITEM = ITEM_IRON_SWORD;

inline constexpr ItemId blockItem(block_type t) {
    return static_cast<ItemId>(t);
}
inline constexpr bool isBlockItem(ItemId id) {
    return id < FIRST_TOOL_ITEM;
}
// The block an item places; AIR for tools (and the empty hand).
inline constexpr block_type itemBlock(ItemId id) {
    return isBlockItem(id) ? static_cast<block_type>(id) : AIR;
}
inline constexpr bool isSword(ItemId id) {
    return id == ITEM_IRON_SWORD;
}
// Accepts block values in the block_type range and known tools, so a
// corrupt level.dat can't put garbage in the hotbar.
inline constexpr bool isValidItem(ItemId id) {
    return id <= static_cast<ItemId>(POPPY) || (id >= FIRST_TOOL_ITEM && id <= LAST_TOOL_ITEM);
}

// Texture-array layer for a block item's icon (its top face); -1 for
// tools, whose icons are standalone images (TextureArray::itemIcon).
inline int itemIconLayer(ItemId id) {
    return isBlockItem(id) ? block_layers::layerForFace(itemBlock(id), 4) : -1;
}
// A tool's 16×16 sprite: the hotbar/inventory icon, and extruded into the
// first-person model (item_mesh.h).
inline const char* toolIconPath(ItemId id) {
    return id == ITEM_IRON_SWORD ? "assets/Textures/items/iron_sword.png" : nullptr;
}

// Melee stats (Minecraft Java attribute values). Damage in half-hearts;
// cooldown = 1 / attack speed: fist 4.0/s, swords 1.6/s.
struct MeleeStats {
    float damage;
    double cooldownSeconds;
};
inline constexpr MeleeStats meleeStats(ItemId id) {
    if (id == ITEM_IRON_SWORD) return {6.0f, 1.0 / 1.6};
    return {1.0f, 1.0 / 4.0};
}
