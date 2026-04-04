/**
 * @file cube.h
 * @brief Defines the Cube class, representing a block in the world.
 */

#pragma once

#define RENDER_DISTANCE 8
#define CHUNK_SIZE 16

#include <glm/glm.hpp>

/**
 * @enum block_type
 */
enum block_type {
    AIR,
    GRASS,
    DIRT,
    STONE,
    COAL_ORE,
    BEDROCK,
    WATER,
    SAND,
    GLOWSTONE
};

/**
 * @class Cube
 * @brief Pure data class — stores block type and world-space position.
 *        All rendering is handled by Chunk mesh batching.
 */
class Cube {
public:
    Cube();
    Cube(int x, int y, int z);
    Cube(int x, int y, int z, block_type type);
    Cube(glm::vec3& position);
    Cube(glm::vec3& position, block_type type);

    void translate(float x, float y, float z);

    glm::vec3 getPosition() const;
    block_type getType() const;

    void setPosition(int x, int y, int z);
    void setPosition(glm::vec3& position);
    void setType(block_type type);

    ~Cube() = default;

protected:
    block_type type;
    glm::vec3 position;
};
