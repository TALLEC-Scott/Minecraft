#pragma once

#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"

#define RENDER_DISTANCE 2

class World {
public:
	World();

	void render(Shader& shaderProgram);
	void destroyBlock(glm::vec3 position);

	~World();
private:
	Chunk* chunks[RENDER_DISTANCE * RENDER_DISTANCE];
    TerrainGenerator* terrainGenerator;
};