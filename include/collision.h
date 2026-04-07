#pragma once

#include <glm/glm.hpp>
#include <cmath>

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
template <typename SolidCheck>
VerticalResult resolveVertical(glm::vec3 feetPos, float moveY, SolidCheck isSolid) {
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
