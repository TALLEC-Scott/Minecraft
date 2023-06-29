#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

#include "cube.h"
#include "shader.h"

#define CHUNK_SIZE 16

class Chunk {
public:
	Chunk();

	void render(Shader shaderProgram);
	void destroy();

	~Chunk();

private:
	std::vector<std::vector<std::vector<Cube>>> blocks;
};