#pragma once

// A dropped item stack lying in the world: pops out of a broken block, a
// dead mob or a dead player, falls and slides like a small box (Minecraft's
// item physics), bobs and spins when drawn, and is picked up by a player
// walking over it. Local only (not saved or networked), like mobs.
// Header-only and world-agnostic (the tick takes a solid-block check), so
// it is unit-tested.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "collision.h"
#include "item.h"

struct ItemEntity {
    static constexpr float TICK_GRAVITY = 0.04f;   // blocks / tick² (camera.h owns GRAVITY)
    static constexpr float DRAG = 0.98f;           // per tick, every axis
    static constexpr float GROUND_FRICTION = 0.6f; // extra horizontal slip on the ground
    static constexpr float HALF_WIDTH = 0.125f;
    static constexpr float HEIGHT = 0.25f;
    static constexpr int PICKUP_DELAY = 10;  // ticks before a fresh drop can be picked up
    static constexpr int THROW_DELAY = 40;   // ... after the player threw/dropped it
    static constexpr int DESPAWN_AGE = 6000; // 5 minutes
    static constexpr float MERGE_RADIUS = 0.5f;

    ItemId id = ITEM_EMPTY;
    int count = 0;
    glm::vec3 pos{0.0f}; // bottom centre (collision.h "feet" convention)
    glm::vec3 prevPos{0.0f};
    glm::vec3 velocity{0.0f}; // blocks / tick
    int age = 0;
    int pickupDelay = PICKUP_DELAY;
    bool onGround = false;
    bool removed = false;
    float spinPhase = 0.0f; // radians, so drops don't all spin in sync

    glm::vec3 renderPos(float partialTick) const { return glm::mix(prevPos, pos, partialTick); }
    bool canBePickedUp() const { return !removed && pickupDelay <= 0 && count > 0; }

    // One 20 TPS step. `isSolid(x, y, z)` takes integer block coordinates
    // (block N spans [N-0.5, N+0.5)).
    template <typename SolidCheck> void tick(SolidCheck isSolid) {
        prevPos = pos;
        if (pickupDelay > 0) --pickupDelay;
        if (++age >= DESPAWN_AGE) removed = true;

        velocity.y -= TICK_GRAVITY;
        glm::vec3 hmove(velocity.x, 0.0f, velocity.z);
        if (hmove.x != 0.0f || hmove.z != 0.0f) {
            glm::vec3 moved = resolveMovementAABB(pos, hmove, HALF_WIDTH, HEIGHT, isSolid);
            if (std::abs(moved.x - pos.x - hmove.x) > 1e-4f) velocity.x = 0.0f;
            if (std::abs(moved.z - pos.z - hmove.z) > 1e-4f) velocity.z = 0.0f;
            pos.x = moved.x;
            pos.z = moved.z;
        }
        VerticalResult vr = resolveVerticalAABB(pos, velocity.y, HALF_WIDTH, HEIGHT, isSolid);
        pos.y = vr.newFeetY;
        onGround = vr.hitGround;
        if (vr.hitGround || vr.hitCeiling) velocity.y = 0.0f;

        velocity *= DRAG;
        if (onGround) {
            velocity.x *= GROUND_FRICTION;
            velocity.z *= GROUND_FRICTION;
        }
    }
};

// Two stacks of the same item close together become one (Minecraft keeps
// the dropped-item count down this way). Moves as much of `from` into
// `into` as fits; `from` is removed once empty.
inline bool mergeItems(ItemEntity& into, ItemEntity& from, int maxStack) {
    if (into.removed || from.removed || into.id != from.id || into.count >= maxStack) return false;
    glm::vec3 d = into.pos - from.pos;
    if (d.x * d.x + d.z * d.z > ItemEntity::MERGE_RADIUS * ItemEntity::MERGE_RADIUS ||
        std::abs(d.y) > ItemEntity::MERGE_RADIUS)
        return false;
    int moved = std::min(from.count, maxStack - into.count);
    into.count += moved;
    from.count -= moved;
    into.pickupDelay = std::max(into.pickupDelay, from.pickupDelay);
    into.age = std::min(into.age, from.age);
    if (from.count <= 0) from.removed = true;
    return moved > 0;
}
