/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "cube.h"
#include "shader.h"
#include "TerrainGenerator.h"
#include "world.h"


#define CHUNK_SIZE 15
class World;
class Chunk {
public:
    Chunk() {

    }

    Chunk(int chunkX, int chunkY, TerrainGenerator& terrainGenerator);



    std::vector<Cube*> render(Shader shaderProgram, World* world);
	void destroy();
	
	void translate(GLfloat x, GLfloat y, GLfloat z);
	void destroyBlock(int x, int y, int z);

    int getLocalHeight(int x, int y);
    int getGlobalHeight(int x, int y);
    Cube* getBlock(int i, int j, int k);
	~Chunk();

private:
	Cube* blocks; /**< The array of blocks within the chunk. */
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    int chunkX;
    int chunkY;

};