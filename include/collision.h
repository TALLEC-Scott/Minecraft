#pragma once

#include <glm/glm.hpp>
#include <cmath>

static constexpr float PLAYER_HALF_WIDTH = 0.3f;
static constexpr float PLAYER_TOTAL_HEIGHT = 1.8f;

// Check if a player hitbox at feetPos overlaps any solid block
template <typename SolidCheck>
bool collides(glm::vec3 feetPos, SolidCheck isSolid) {
    float hw = PLAYER_HALF_WIDTH;
    int minX = (int)std::floor(feetPos.x - hw);
    int maxX = (int)std::floor(feetPos.x + hw - 0.001f);
    int minY = (int)std::floor(feetPos.y);
    int maxY = (int)std::floor(feetPos.y + PLAYER_TOTAL_HEIGHT - 0.001f);
    int minZ = (int)std::floor(feetPos.z - hw);
    int maxZ = (int)std::floor(feetPos.z + hw - 0.001f);

    for (int bx = minX; bx <= maxX; bx++)
        for (int by = minY; by <= maxY; by++)
            for (int bz = minZ; bz <= maxZ; bz++)
                if (isSolid(bx, by, bz)) return true;
    return false;
}

// Resolve horizontal movement with wall sliding
template <typename SolidCheck>
glm::vec3 resolveMovement(glm::vec3 feetPos, glm::vec3 move, SolidCheck isSolid) {
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
