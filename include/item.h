#pragma once

// What an inventory slot holds: a placeable block or an item (tool, food,
// material). Block items reuse their block_type value (0..255), so
// hotbars saved in level.dat before items existed load unchanged; other
// items start at FIRST_ITEM and are described by itemInfo().

#include <cstdint>

#include "block_layers.h"
#include "cube.h"

using ItemId = uint16_t;

inline constexpr ItemId ITEM_EMPTY = AIR;
inline constexpr ItemId FIRST_ITEM = 256;
// Values are saved in level.dat: append only, never renumber.
inline constexpr ItemId ITEM_IRON_SWORD = 256;
inline constexpr ItemId ITEM_STICK = 257;
inline constexpr ItemId ITEM_COAL = 258;
inline constexpr ItemId ITEM_APPLE = 259;
inline constexpr ItemId ITEM_WOODEN_PICKAXE = 260;
inline constexpr ItemId ITEM_WOODEN_AXE = 261;
inline constexpr ItemId ITEM_WOODEN_SHOVEL = 262;
inline constexpr ItemId ITEM_WOODEN_SWORD = 263;
inline constexpr ItemId ITEM_STONE_PICKAXE = 264;
inline constexpr ItemId ITEM_STONE_AXE = 265;
inline constexpr ItemId ITEM_STONE_SHOVEL = 266;
inline constexpr ItemId ITEM_STONE_SWORD = 267;
inline constexpr ItemId ITEM_RAW_PORKCHOP = 268;
inline constexpr ItemId ITEM_RAW_BEEF = 269;
inline constexpr ItemId ITEM_RAW_CHICKEN = 270;
inline constexpr ItemId ITEM_RAW_MUTTON = 271;
inline constexpr ItemId LAST_ITEM = ITEM_RAW_MUTTON;

enum class ToolKind : uint8_t { None, Pickaxe, Axe, Shovel, Sword };

// Melee stats (Minecraft Java attribute values). Damage in half-hearts;
// cooldown = 1 / attack speed: fist 4.0/s, swords 1.6/s.
struct MeleeStats {
    float damage;
    double cooldownSeconds;
};

// What eating one restores (Minecraft values): food points (half
// drumsticks) and saturation.
struct FoodStats {
    int hunger;
    float saturation;
};

struct ItemInfo {
    const char* name;
    // 16×16 sprite: the hotbar/inventory icon, extruded into the held
    // model (item_mesh.h) and drawn spinning when dropped.
    const char* iconPath;
    ToolKind tool;
    int tier; // 1 wood, 2 stone, 3 iron; 0 for non-tools
    MeleeStats melee;
    FoodStats food; // hunger 0 = not edible
};

inline constexpr MeleeStats FIST_MELEE{1.0f, 1.0 / 4.0};

// nullptr for blocks and unknown ids.
inline const ItemInfo* itemInfo(ItemId id) {
    // clang-format off
    static const ItemInfo items[] = {
        /* 256 */ {"Iron Sword",     "assets/Textures/items/iron_sword.png",     ToolKind::Sword,   3, {6.0f, 1.0 / 1.6}, {}},
        /* 257 */ {"Stick",          "assets/Textures/items/stick.png",          ToolKind::None,    0, FIST_MELEE,        {}},
        /* 258 */ {"Coal",           "assets/Textures/items/coal.png",           ToolKind::None,    0, FIST_MELEE,        {}},
        /* 259 */ {"Apple",          "assets/Textures/items/apple.png",          ToolKind::None,    0, FIST_MELEE,        {4, 2.4f}},
        /* 260 */ {"Wooden Pickaxe", "assets/Textures/items/wooden_pickaxe.png", ToolKind::Pickaxe, 1, {2.0f, 1.0 / 1.2}, {}},
        /* 261 */ {"Wooden Axe",     "assets/Textures/items/wooden_axe.png",     ToolKind::Axe,     1, {7.0f, 1.0 / 0.8}, {}},
        /* 262 */ {"Wooden Shovel",  "assets/Textures/items/wooden_shovel.png",  ToolKind::Shovel,  1, {2.5f, 1.0 / 1.0}, {}},
        /* 263 */ {"Wooden Sword",   "assets/Textures/items/wooden_sword.png",   ToolKind::Sword,   1, {4.0f, 1.0 / 1.6}, {}},
        /* 264 */ {"Stone Pickaxe",  "assets/Textures/items/stone_pickaxe.png",  ToolKind::Pickaxe, 2, {3.0f, 1.0 / 1.2}, {}},
        /* 265 */ {"Stone Axe",      "assets/Textures/items/stone_axe.png",      ToolKind::Axe,     2, {9.0f, 1.0 / 0.8}, {}},
        /* 266 */ {"Stone Shovel",   "assets/Textures/items/stone_shovel.png",   ToolKind::Shovel,  2, {3.5f, 1.0 / 1.0}, {}},
        /* 267 */ {"Stone Sword",    "assets/Textures/items/stone_sword.png",    ToolKind::Sword,   2, {5.0f, 1.0 / 1.6}, {}},
        /* 268 */ {"Raw Porkchop",   "assets/Textures/items/raw_porkchop.png",   ToolKind::None,    0, FIST_MELEE,        {3, 1.8f}},
        /* 269 */ {"Raw Beef",       "assets/Textures/items/raw_beef.png",       ToolKind::None,    0, FIST_MELEE,        {3, 1.8f}},
        /* 270 */ {"Raw Chicken",    "assets/Textures/items/raw_chicken.png",    ToolKind::None,    0, FIST_MELEE,        {2, 1.2f}},
        /* 271 */ {"Raw Mutton",     "assets/Textures/items/raw_mutton.png",     ToolKind::None,    0, FIST_MELEE,        {2, 1.2f}},
    };
    // clang-format on
    static_assert(sizeof(items) / sizeof(items[0]) == LAST_ITEM - FIRST_ITEM + 1, "one row per item id");
    if (id < FIRST_ITEM || id > LAST_ITEM) return nullptr;
    return &items[id - FIRST_ITEM];
}

inline constexpr ItemId blockItem(block_type t) {
    return static_cast<ItemId>(t);
}
inline constexpr bool isBlockItem(ItemId id) {
    return id < FIRST_ITEM;
}
// The block an item places; AIR for items (and the empty hand).
inline constexpr block_type itemBlock(ItemId id) {
    return isBlockItem(id) ? static_cast<block_type>(id) : AIR;
}
inline ToolKind toolKind(ItemId id) {
    const ItemInfo* info = itemInfo(id);
    return info ? info->tool : ToolKind::None;
}
inline int toolTier(ItemId id) {
    const ItemInfo* info = itemInfo(id);
    return info ? info->tier : 0;
}
inline bool isTool(ItemId id) {
    return toolKind(id) != ToolKind::None;
}
inline bool isSword(ItemId id) {
    return toolKind(id) == ToolKind::Sword;
}
inline FoodStats foodStats(ItemId id) {
    const ItemInfo* info = itemInfo(id);
    return info ? info->food : FoodStats{};
}
inline bool isFood(ItemId id) {
    return foodStats(id).hunger > 0;
}
// Accepts block values in the block_type range and known items, so a
// corrupt level.dat can't put garbage in the inventory.
inline bool isValidItem(ItemId id) {
    return id < static_cast<ItemId>(BLOCK_TYPE_COUNT) || itemInfo(id) != nullptr;
}

// Display name (inventory tooltips, the item wiki).
inline const char* itemName(ItemId id) {
    if (const ItemInfo* info = itemInfo(id)) return info->name;
    switch (itemBlock(id)) {
    case GRASS:
        return "Grass Block";
    case DIRT:
        return "Dirt";
    case STONE:
        return "Stone";
    case COAL_ORE:
        return "Coal Ore";
    case BEDROCK:
        return "Bedrock";
    case WATER:
        return "Water";
    case SAND:
        return "Sand";
    case GLOWSTONE:
        return "Glowstone";
    case WOOD:
        return "Wood";
    case LEAVES:
        return "Leaves";
    case SNOW:
        return "Snow";
    case GRAVEL:
        return "Gravel";
    case CACTUS:
        return "Cactus";
    case TNT:
        return "TNT";
    case TALL_GRASS:
        return "Tall Grass";
    case DANDELION:
        return "Dandelion";
    case POPPY:
        return "Poppy";
    case PLANKS:
        return "Oak Planks";
    case COBBLESTONE:
        return "Cobblestone";
    case CRAFTING_TABLE:
        return "Crafting Table";
    default:
        return "?";
    }
}

// Texture-array layer for a block item's icon (its top face); -1 for
// other items, whose icons are standalone images (TextureArray::itemIcon).
inline int itemIconLayer(ItemId id) {
    return isBlockItem(id) ? block_layers::layerForFace(itemBlock(id), 4) : -1;
}
// An item's sprite path; nullptr for blocks.
inline const char* itemIconPath(ItemId id) {
    const ItemInfo* info = itemInfo(id);
    return info ? info->iconPath : nullptr;
}

// What something drops when broken or killed (count 0 = nothing).
struct ItemDrop {
    ItemId id = ITEM_EMPTY;
    int count = 0;
};

inline MeleeStats meleeStats(ItemId id) {
    const ItemInfo* info = itemInfo(id);
    return info ? info->melee : FIST_MELEE;
}
