#pragma once

#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"

#define RENDER_DISTANCE 3
class Chunk;

class World {
public:
	World();

	void render(Shader& shaderProgram);
	void destroyBlock(glm::vec3 position);
    Chunk* getChunk(int x, int y);
    Cube* getBlock(int x, int y, int z);

	~World();
private:
	Chunk* chunks[RENDER_DISTANCE * RENDER_DISTANCE];
    TerrainGenerator* terrainGenerator;
};