#pragma once

#include <glm/glm.hpp>
#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"
#include "ChunkManager.h"

class World {
  public:
    World(unsigned int seed = 0);

    // void render(Shader& shaderProgram);
    void destroyBlock(glm::vec3 position) const;
    void placeBlock(glm::ivec3 position, block_type type) const;
    Chunk* getChunk(int x, int y);
    Cube* getBlock(int x, int y, int z) const;
    TerrainGenerator* terrainGenerator;
    ChunkManager* chunkManager;
    ~World();

    int render(const Shader& shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const;
    void update(glm::vec3 cameraPosition) const;
    // Raycast: returns true if a block was hit, sets hitPos to the block and prevPos to the air block before it
    bool raycast(glm::vec3 origin, glm::vec3 direction, float maxDist, glm::ivec3& hitPos, glm::ivec3& prevPos) const;
};