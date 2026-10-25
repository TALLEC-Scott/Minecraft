#pragma once

// What a mob drops when it dies (Minecraft's loot, minus the items this
// game doesn't have: leather, feathers, wool, rotten flesh, bones...).
// Pure, so it is unit-tested; MobManager spawns the item when a dying mob
// finishes its death animation.

#include "item.h"
#include "mob_registry.h"

// `roll` is a uniform [0, 1) random number that picks the count.
inline ItemDrop mobDrop(MobType type, float roll) {
    auto between = [roll](int lo, int hi) { return lo + static_cast<int>(roll * static_cast<float>(hi - lo + 1)); };
    switch (type) {
    case MobType::Pig:
        return {ITEM_RAW_PORKCHOP, between(1, 3)};
    case MobType::Cow:
        return {ITEM_RAW_BEEF, between(1, 3)};
    case MobType::Sheep:
        return {ITEM_RAW_MUTTON, between(1, 2)};
    case MobType::Chicken:
        return {ITEM_RAW_CHICKEN, 1};
    default:
        return {};
    }
}
