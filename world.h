#pragma once

#include "chunk.h"
#include "shader.h"

#define RENDER_DISTANCE 2

class World {
public:
	World();

	void initialize();
	void render(Shader& shaderProgram);
	void destroyBlock(glm::vec3 position);

	~World();
private:
	Chunk* chunks;
};