/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#include <random>
#include "chunk.h"
//#include "perlin_noise.h"

double generateRandomNumber(double probabilityOfOne) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<double> dis(0.0, 1.0);

	if (dis(gen) < probabilityOfOne) {
		return 1.0;
	}
	else {
		return dis(gen);
	}
}

Chunk::Chunk(int chunkX, int chunkY, TerrainGenerator& terrain) {
	blocks = new Cube[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
                int globalX = chunkX * CHUNK_SIZE + i;
                int globalY = chunkY * CHUNK_SIZE + k;
                int height = terrain.getHeight(globalX, globalY);
                heights[i][k] = height;
                std::cout << height << std::endl;

                Cube* block = &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];
				block->setPosition(i, j, k);
				int limit_grass = (int)(0.95 * CHUNK_SIZE) < 1 ? CHUNK_SIZE - 1 : (int)(0.95 * CHUNK_SIZE);
				int limit_stone = (int)(0.7 * CHUNK_SIZE) < 1 ? CHUNK_SIZE - 2 : (int)(0.7 * CHUNK_SIZE);
				double p = generateRandomNumber(0.01f);
				if (j > height) {
					block->setType(AIR);
				}
				else {
					if (j == 0)
						block->setType(BEDROCK);
					else if (j < limit_stone)
						if (p == 1.0)
							block->setType(COAL_ORE);
						else
							block->setType(STONE);
					else if (j < limit_grass)
						block->setType(DIRT);
					else
						block->setType(GRASS);
				}
			}
		}
	}
}

Cube* Chunk::getBlock(int i, int j, int k) {
	if ((i < 0 || i >= CHUNK_SIZE) || (j < 0 || j >= CHUNK_SIZE) || (k < 0 || k >= CHUNK_SIZE)) {
		return nullptr;
	}
	return &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];
}

void Chunk::render(Shader shaderProgram) {
	glEnable(GL_CULL_FACE);
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
				Cube* block = &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];

				shaderProgram.setVec3("translation", block->getPosition());
				shaderProgram.use();

				// V�rifier les voisins et ne pas afficher les faces en contact
				std::vector<Cube*> neighbors = {
					getBlock(i, j, k + 1),  // Face avant
					getBlock(i, j, k - 1),  // Face arri�re
					getBlock(i - 1, j, k),  // Face gauche
					getBlock(i + 1, j, k),  // Face droite
					getBlock(i, j + 1, k),  // Face haut
					getBlock(i, j - 1, k)   // Face bas
				};
				block->render(neighbors);
			}
		}
	}
}

void Chunk::translate(GLfloat x, GLfloat y, GLfloat z) {
	for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
		blocks[i].translate(x * CHUNK_SIZE, y * CHUNK_SIZE, z * CHUNK_SIZE);
	}
}

void Chunk::destroy() {
	for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
		blocks[i].destroy();
	}
	delete[] blocks;
}

void Chunk::destroyBlock(int x, int y, int z) {
	blocks[x * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + z].setType(AIR);
}

int Chunk::getLocalHeight(int x, int y) {
    return heights[x][y];
}

int Chunk::getGlobalHeight(int x, int y) {
    return heights[x % CHUNK_SIZE][y % CHUNK_SIZE];
}


Chunk::~Chunk() {
	destroy();
}