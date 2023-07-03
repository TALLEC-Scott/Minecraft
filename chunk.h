/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "cube.h"
#include "shader.h"

#define CHUNK_SIZE 15

class Chunk {
public:
	Chunk();

	void render(Shader shaderProgram);
	void destroy();
	
	void translate(GLfloat x, GLfloat y, GLfloat z);
	void destroyBlock(int x, int y, int z);

	~Chunk();

private:
	Cube* blocks; /**< The array of blocks within the chunk. */

	Cube* getBlock(int i, int j, int k);
};