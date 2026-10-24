#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <utility>

static constexpr float PLAYER_HALF_WIDTH = 0.3f;
static constexpr float PLAYER_TOTAL_HEIGHT = 1.8f;

// Check if a player hitbox at feetPos overlaps any solid block
// Blocks are centered on integers: block N spans [N-0.5, N+0.5), so shift by +0.5
template <typename SolidCheck> bool collides(glm::vec3 feetPos, SolidCheck isSolid) {
    float hw = PLAYER_HALF_WIDTH;
    int minX = (int)std::floor(feetPos.x - hw + 0.5f);
    int maxX = (int)std::floor(feetPos.x + hw + 0.5f - 0.001f);
    int minY = (int)std::floor(feetPos.y + 0.5f);
    int maxY = (int)std::floor(feetPos.y + PLAYER_TOTAL_HEIGHT + 0.5f - 0.001f);
    int minZ = (int)std::floor(feetPos.z - hw + 0.5f);
    int maxZ = (int)std::floor(feetPos.z + hw + 0.5f - 0.001f);

    for (int bx = minX; bx <= maxX; bx++)
        for (int by = minY; by <= maxY; by++)
            for (int bz = minZ; bz <= maxZ; bz++)
                if (isSolid(bx, by, bz)) return true;
    return false;
}

struct VerticalResult {
    float newFeetY;
    bool hitGround;
    bool hitCeiling;
};

// Resolve vertical movement with AABB — checks all blocks the player hitbox overlaps
template <typename SolidCheck> VerticalResult resolveVertical(glm::vec3 feetPos, float moveY, SolidCheck isSolid) {
    VerticalResult result = {feetPos.y + moveY, false, false};
    float hw = PLAYER_HALF_WIDTH;
    int minX = (int)std::floor(feetPos.x - hw + 0.5f);
    int maxX = (int)std::floor(feetPos.x + hw + 0.5f - 0.001f);
    int minZ = (int)std::floor(feetPos.z - hw + 0.5f);
    int maxZ = (int)std::floor(feetPos.z + hw + 0.5f - 0.001f);

    if (moveY < 0) {
        // Falling: check blocks below feet
        int newFeetBlockY = (int)std::floor(feetPos.y + moveY + 0.5f);
        int oldFeetBlockY = (int)std::floor(feetPos.y + 0.5f);
        for (int by = oldFeetBlockY - 1; by >= newFeetBlockY; by--) {
            for (int bx = minX; bx <= maxX; bx++) {
                for (int bz = minZ; bz <= maxZ; bz++) {
                    if (isSolid(bx, by, bz)) {
                        result.newFeetY = (float)by + 0.5f;
                        result.hitGround = true;
                        return result;
                    }
                }
            }
        }
    } else if (moveY > 0) {
        // Jumping: check blocks above head
        float headY = feetPos.y + PLAYER_TOTAL_HEIGHT;
        int oldHeadBlockY = (int)std::floor(headY + 0.5f - 0.001f);
        int newHeadBlockY = (int)std::floor(feetPos.y + moveY + PLAYER_TOTAL_HEIGHT + 0.5f - 0.001f);
        for (int by = oldHeadBlockY + 1; by <= newHeadBlockY; by++) {
            for (int bx = minX; bx <= maxX; bx++) {
                for (int bz = minZ; bz <= maxZ; bz++) {
                    if (isSolid(bx, by, bz)) {
                        result.newFeetY = (float)by - 0.5f - PLAYER_TOTAL_HEIGHT;
                        result.hitCeiling = true;
                        return result;
                    }
                }
            }
        }
    }
    return result;
}

// Generic AABB variants for non-player entities (e.g. primed TNT — 1×1×1 hitbox).
// `halfW` and `height` parameterize the bounding box; semantics otherwise match
// the player helpers above. Keeping them as free templates lets entities reuse
// the voxel-grid walk without dragging the player-specific half-width/height
// constants into their code paths.
template <typename SolidCheck> bool collidesAABB(glm::vec3 feetPos, float halfW, float height, SolidCheck isSolid) {
    int minX = (int)std::floor(feetPos.x - halfW + 0.5f);
    int maxX = (int)std::floor(feetPos.x + halfW + 0.5f - 0.001f);
    int minY = (int)std::floor(feetPos.y + 0.5f);
    int maxY = (int)std::floor(feetPos.y + height + 0.5f - 0.001f);
    int minZ = (int)std::floor(feetPos.z - halfW + 0.5f);
    int maxZ = (int)std::floor(feetPos.z + halfW + 0.5f - 0.001f);
    for (int bx = minX; bx <= maxX; bx++)
        for (int by = minY; by <= maxY; by++)
            for (int bz = minZ; bz <= maxZ; bz++)
                if (isSolid(bx, by, bz)) return true;
    return false;
}

template <typename SolidCheck>
VerticalResult resolveVerticalAABB(glm::vec3 feetPos, float moveY, float halfW, float height, SolidCheck isSolid) {
    VerticalResult result = {feetPos.y + moveY, false, false};
    int minX = (int)std::floor(feetPos.x - halfW + 0.5f);
    int maxX = (int)std::floor(feetPos.x + halfW + 0.5f - 0.001f);
    int minZ = (int)std::floor(feetPos.z - halfW + 0.5f);
    int maxZ = (int)std::floor(feetPos.z + halfW + 0.5f - 0.001f);
    if (moveY < 0) {
        int newFeetBlockY = (int)std::floor(feetPos.y + moveY + 0.5f);
        int oldFeetBlockY = (int)std::floor(feetPos.y + 0.5f);
        for (int by = oldFeetBlockY - 1; by >= newFeetBlockY; by--) {
            for (int bx = minX; bx <= maxX; bx++) {
                for (int bz = minZ; bz <= maxZ; bz++) {
                    if (isSolid(bx, by, bz)) {
                        result.newFeetY = (float)by + 0.5f;
                        result.hitGround = true;
                        return result;
                    }
                }
            }
        }
    } else if (moveY > 0) {
        float headY = feetPos.y + height;
        int oldHeadBlockY = (int)std::floor(headY + 0.5f - 0.001f);
        int newHeadBlockY = (int)std::floor(feetPos.y + moveY + height + 0.5f - 0.001f);
        for (int by = oldHeadBlockY + 1; by <= newHeadBlockY; by++) {
            for (int bx = minX; bx <= maxX; bx++) {
                for (int bz = minZ; bz <= maxZ; bz++) {
                    if (isSolid(bx, by, bz)) {
                        result.newFeetY = (float)by - 0.5f - height;
                        result.hitCeiling = true;
                        return result;
                    }
                }
            }
        }
    }
    return result;
}

template <typename SolidCheck>
glm::vec3 resolveMovementAABB(glm::vec3 feetPos, glm::vec3 move, float halfW, float height, SolidCheck isSolid) {
    glm::vec3 both = {feetPos.x + move.x, feetPos.y, feetPos.z + move.z};
    if (!collidesAABB(both, halfW, height, isSolid)) return both;
    glm::vec3 result = feetPos;
    glm::vec3 tryX = {feetPos.x + move.x, feetPos.y, feetPos.z};
    if (!collidesAABB(tryX, halfW, height, isSolid)) result.x = tryX.x;
    glm::vec3 tryZ = {result.x, feetPos.y, feetPos.z + move.z};
    if (!collidesAABB(tryZ, halfW, height, isSolid)) result.z = tryZ.z;
    return result;
}

// Resolve horizontal movement with wall sliding
template <typename SolidCheck> glm::vec3 resolveMovement(glm::vec3 feetPos, glm::vec3 move, SolidCheck isSolid) {
    // Try both axes
    glm::vec3 both = {feetPos.x + move.x, feetPos.y, feetPos.z + move.z};
    if (!collides(both, isSolid)) return both;

    // Slide: try each axis
    glm::vec3 result = feetPos;
    glm::vec3 tryX = {feetPos.x + move.x, feetPos.y, feetPos.z};
    if (!collides(tryX, isSolid)) result.x = tryX.x;

    glm::vec3 tryZ = {result.x, feetPos.y, feetPos.z + move.z};
    if (!collides(tryZ, isSolid)) result.z = tryZ.z;

    return result;
}

// Slab-method ray vs. axis-aligned box. `dir` need not be normalized; `outT`
// is the entry parameter along `dir` (0 when the origin is inside the box).
// Returns false when the box is missed or the hit lies beyond `maxT`.
inline bool rayIntersectsAABB(glm::vec3 origin, glm::vec3 dir, glm::vec3 boxMin, glm::vec3 boxMax, float maxT,
                              float& outT) {
    float tNear = 0.0f;
    float tFar = maxT;
    for (int i = 0; i < 3; i++) {
        if (std::abs(dir[i]) < 1e-8f) {
            if (origin[i] < boxMin[i] || origin[i] > boxMax[i]) return false;
            continue;
        }
        float inv = 1.0f / dir[i];
        float t0 = (boxMin[i] - origin[i]) * inv;
        float t1 = (boxMax[i] - origin[i]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tNear) tNear = t0;
        if (t1 < tFar) tFar = t1;
        if (tNear > tFar) return false;
    }
    outT = tNear;
    return true;
}
