/**
 * @file cube.cpp
 * @brief Pure data implementation — position and block type only.
 *        Rendering is handled by Chunk::buildMesh().
 */

#include "cube.h"

Cube::Cube() : position(0, 0, 0), type(AIR) {}

Cube::Cube(int x, int y, int z) : position(x, y, z), type(AIR) {}

Cube::Cube(int x, int y, int z, block_type type) : position(x, y, z), type(type) {}

Cube::Cube(glm::vec3& position) : position(position), type(AIR) {}

Cube::Cube(glm::vec3& position, block_type type) : position(position), type(type) {}

void Cube::translate(float x, float y, float z) {
    position.x += x;
    position.y += y;
    position.z += z;
}

glm::vec3 Cube::getPosition() const {
    return position;
}

block_type Cube::getType() const {
    return type;
}

void Cube::setPosition(int x, int y, int z) {
    position = glm::vec3(x, y, z);
}

void Cube::setPosition(glm::vec3& pos) {
    position = pos;
}

void Cube::setType(block_type t) {
    type = t;
}
