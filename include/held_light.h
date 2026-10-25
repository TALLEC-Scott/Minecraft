#pragma once

// Light carried in hand (what the "dynamic lights" mods add to Minecraft):
// a player holding a torch / glowstone lights their surroundings. Done in
// the shaders, not the world's light data: each fragment takes the larger
// of its stored block light and `level - distance` from the holder, so it
// follows the player smoothly without relighting or remeshing chunks.
// Faces turned away from the holder are skipped (frag.shd) so a wall's far
// side stays dark; light still reaches around thin walls, unlike placed
// torches.

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

#include "cube.h"
#include "item.h"

struct HeldLight {
    glm::vec3 pos;
    float level; // 0..15, like a block's emission
};

// frag.shd's array size: the local player plus a few remote ones.
inline constexpr int MAX_HELD_LIGHTS = 4;

// Light a held item gives off: its block's emission (torch 14, glowstone
// 15), 0 for anything else.
inline int heldItemLight(ItemId id) {
    return isBlockItem(id) ? getBlockLightEmission(itemBlock(id)) : 0;
}

// Block-light level (0..15) the held lights give at `p` (CPU side, for
// things drawn outside the chunk shader: the arm, held and dropped items).
inline float heldLightLevelAt(const std::vector<HeldLight>& lights, glm::vec3 p) {
    float best = 0.0f;
    for (const HeldLight& l : lights) best = std::max(best, l.level - glm::distance(l.pos, p));
    return std::clamp(best, 0.0f, 15.0f);
}
