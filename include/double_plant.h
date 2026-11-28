#pragma once

// Two-block-tall plants (Minecraft's double_plant: tall grass, large fern).
// Each plant is two stacked cells of two block types, a lower half (the
// item, what world generation and players place) and an upper half that
// only ever sits on top of it. Separate types rather than a half bit in the
// state byte: world generation writes block types only, each half picks its
// own texture by type with no mesher change, and a network peer that only
// gets a block type still knows which half it is.
//
// Breaking either half breaks the other (World::destroyBlock); placing a
// lower half places its upper half too (World::placeBlock), so network
// echoes of either edit leave both peers with whole plants.

#include "cube.h"

namespace double_plant {

inline bool isLower(block_type t) {
    return t == DOUBLE_TALL_GRASS || t == LARGE_FERN;
}
inline bool isUpper(block_type t) {
    return t == DOUBLE_TALL_GRASS_TOP || t == LARGE_FERN_TOP;
}
inline bool isHalf(block_type t) {
    return isLower(t) || isUpper(t);
}

// The upper half that goes on lower half `t` (AIR if `t` isn't one).
inline block_type upperOf(block_type t) {
    if (t == DOUBLE_TALL_GRASS) return DOUBLE_TALL_GRASS_TOP;
    if (t == LARGE_FERN) return LARGE_FERN_TOP;
    return AIR;
}
inline block_type lowerOf(block_type t) {
    if (t == DOUBLE_TALL_GRASS_TOP) return DOUBLE_TALL_GRASS;
    if (t == LARGE_FERN_TOP) return LARGE_FERN;
    return AIR;
}
// The one-block version, for where there's no room for the upper half.
inline block_type shortOf(block_type t) {
    if (t == DOUBLE_TALL_GRASS || t == DOUBLE_TALL_GRASS_TOP) return TALL_GRASS;
    if (t == LARGE_FERN || t == LARGE_FERN_TOP) return FERN;
    return t;
}

// The other half of a plant cell of type `t`: its vertical offset (+1 for a
// lower half, -1 for an upper half) and the type it must have there.
// False if `t` isn't a half.
inline bool partner(block_type t, int& dy, block_type& other) {
    if (isLower(t)) {
        dy = 1;
        other = upperOf(t);
        return true;
    }
    if (isUpper(t)) {
        dy = -1;
        other = lowerOf(t);
        return true;
    }
    return false;
}

} // namespace double_plant
