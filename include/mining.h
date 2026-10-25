#pragma once

// Survival block breaking, with Minecraft Java's rules: how long a block
// takes to break with what's in hand, and what it drops. Pure, so it is
// unit-tested; Player drives it at 20 ticks per second.

#include <cmath>

#include "cube.h"
#include "item.h"

struct BlockMining {
    float hardness;     // < 0 = unbreakable
    ToolKind bestTool;  // the tool kind that speeds it up
    bool needsTool;     // drops nothing unless broken with bestTool
    int minTier = 0;    // ... of at least this tier (obsidian: above stone)
};

inline BlockMining blockMining(block_type t) {
    // clang-format off
    static const BlockMining table[] = {
        /* AIR            */ {-1.0f, ToolKind::None,    false},
        /* GRASS          */ {0.6f,  ToolKind::Shovel,  false},
        /* DIRT           */ {0.5f,  ToolKind::Shovel,  false},
        /* STONE          */ {1.5f,  ToolKind::Pickaxe, true},
        /* COAL_ORE       */ {3.0f,  ToolKind::Pickaxe, true},
        /* BEDROCK        */ {-1.0f, ToolKind::None,    false},
        /* WATER          */ {-1.0f, ToolKind::None,    false},
        /* SAND           */ {0.5f,  ToolKind::Shovel,  false},
        /* GLOWSTONE      */ {0.3f,  ToolKind::None,    false},
        /* WOOD           */ {2.0f,  ToolKind::Axe,     false},
        /* LEAVES         */ {0.2f,  ToolKind::None,    false},
        /* SNOW           */ {0.2f,  ToolKind::Shovel,  false},
        /* GRAVEL         */ {0.6f,  ToolKind::Shovel,  false},
        /* CACTUS         */ {0.4f,  ToolKind::None,    false},
        /* TNT            */ {0.0f,  ToolKind::None,    false},
        /* TALL_GRASS     */ {0.0f,  ToolKind::None,    false},
        /* DANDELION      */ {0.0f,  ToolKind::None,    false},
        /* POPPY          */ {0.0f,  ToolKind::None,    false},
        /* PLANKS         */ {2.0f,  ToolKind::Axe,     false},
        /* COBBLESTONE    */ {2.0f,  ToolKind::Pickaxe, true},
        /* CRAFTING_TABLE */ {2.5f,  ToolKind::Axe,     false},
        /* LAVA           */ {-1.0f, ToolKind::None,    false},
        // Minecraft needs a diamond pickaxe; nothing here is good enough yet.
        /* OBSIDIAN       */ {50.0f, ToolKind::Pickaxe, true, 4},
        /* TORCH          */ {0.0f,  ToolKind::None,    false},
    };
    // clang-format on
    static_assert(sizeof(table) / sizeof(table[0]) == BLOCK_TYPE_COUNT, "one row per block_type");
    int i = static_cast<int>(t);
    if (i < 0 || i >= BLOCK_TYPE_COUNT) return {-1.0f, ToolKind::None, false};
    return table[i];
}

// Mining speed of `held` on `t`: the tool's tier speed when it is the
// block's best tool (wood 2, stone 4, iron 6), else 1 (bare hand).
inline float miningSpeed(block_type t, ItemId held) {
    ToolKind kind = toolKind(held);
    if (kind == ToolKind::None || kind != blockMining(t).bestTool) return 1.0f;
    static const float TIER_SPEED[] = {1.0f, 2.0f, 4.0f, 6.0f};
    int tier = toolTier(held);
    return (tier >= 0 && tier <= 3) ? TIER_SPEED[tier] : 1.0f;
}

// Whether breaking `t` with `held` yields its drop.
inline bool canHarvest(block_type t, ItemId held) {
    BlockMining m = blockMining(t);
    return !m.needsTool || (toolKind(held) == m.bestTool && toolTier(held) >= m.minTier);
}

// Ticks (1/20 s) to break `t` with `held`: Minecraft's
// ceil(1 / (speed / hardness / (harvestable ? 30 : 100))). 0 = instant,
// -1 = unbreakable.
inline int breakTicks(block_type t, ItemId held) {
    BlockMining m = blockMining(t);
    if (m.hardness < 0.0f) return -1;
    if (m.hardness == 0.0f) return 0;
    float perTick = miningSpeed(t, held) / m.hardness / (canHarvest(t, held) ? 30.0f : 100.0f);
    if (perTick >= 1.0f) return 0;
    // The epsilon keeps exact fractions (dirt: 15.0) from rounding up a tick.
    return static_cast<int>(std::ceil(1.0f / perTick - 1e-4f));
}

// What breaking `t` with `held` drops. `roll` is a uniform [0, 1) random
// number for chance drops (leaves → apple 1 in 200).
inline ItemDrop blockDrop(block_type t, ItemId held, float roll) {
    if (!canHarvest(t, held) || blockMining(t).hardness < 0.0f) return {};
    switch (t) {
    case GRASS:
        return {blockItem(DIRT), 1};
    case STONE:
        return {blockItem(COBBLESTONE), 1};
    case COAL_ORE:
        return {ITEM_COAL, 1};
    case LEAVES:
        return roll < 0.005f ? ItemDrop{ITEM_APPLE, 1} : ItemDrop{};
    case TALL_GRASS:
        return {};
    default:
        return {blockItem(t), 1};
    }
}
