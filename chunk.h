#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "cube.h"
#include "shader.h"

#define CHUNK_SIZE 16

class Chunk {
public:
	Chunk();

	void render(Shader shaderProgram);
	void destroy();
	
	void translate(GLfloat x, GLfloat y, GLfloat z);

	~Chunk();

private:
	Cube* getBlock(int i, int j, int k);
	Cube* blocks;
};