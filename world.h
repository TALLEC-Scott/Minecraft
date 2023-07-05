#pragma once

#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"
#include "ChunkManager.h"


class World {
public:
	World();

    //void render(Shader& shaderProgram);
	void destroyBlock(glm::vec3 position) const;
    Chunk* getChunk(int x, int y);
    Cube* getBlock(int x, int y, int z) const;
    TerrainGenerator* terrainGenerator;
    ChunkManager* chunkManager;
	~World();

    void render(Shader shaderProgram) const;
    void update(glm::vec3 cameraPosition) const;
};