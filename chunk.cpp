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
    this->chunkX = chunkX;
    this->chunkY = chunkY;
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
                int globalX = chunkX * CHUNK_SIZE + i;
                int globalY = chunkY * CHUNK_SIZE + k;

                int height = terrain.getHeight(globalX, globalY);
                heights[i][k] = height;


                Cube* block = &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];
				block->setPosition(i, j, k);
				int limit_grass = (int)(0.95 * height) < 1 ? height - 1 : (int)(0.95 * height);
				int limit_stone = (int)(0.7 * height) < 1 ? height - 2 : (int)(0.7 * height);

                double detailNoise = terrain.getNoise(globalX, globalY, j);
				int limit_water = (int)(0.5 * CHUNK_SIZE);
				if (j > height) {
					if (j == limit_water) {
						block->setType(WATER);
					}
					else {
						block->setType(AIR);
					}

				}
				else {
					std::vector<Cube*> neighbors = {
							getBlock(i, j, k + 1),  // Face avant
							getBlock(i, j, k - 1),  // Face arrière
							getBlock(i - 1, j, k),  // Face gauche
							getBlock(i + 1, j, k),  // Face droite
					};
					bool beach = false;
					for (int a = 0; a < neighbors.size(); a++) {
						Cube* neighbor = neighbors.at(a);
						if (neighbor != nullptr && neighbor->getType() == WATER) {
							beach = true;
							break;
						}
					}
					if (j == 0)
						block->setType(BEDROCK);
					else if (j == limit_water && beach)
						block->setType(SAND);
					else if (j < limit_stone)
                        if (detailNoise < 0.5 && detailNoise > 0.45) {
                            block->setType(COAL_ORE);
                        } else {
                            block->setType(STONE);
                        }
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


std::vector<Cube*> Chunk::render(Shader shaderProgram, World* world) {
	std::vector<Cube*> waterBlocks;

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
				Cube* block = &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];
				if (block->getType() == WATER) {
					waterBlocks.push_back(block);
				}
				else {
					shaderProgram.setVec3("translation", block->getPosition());
					shaderProgram.use();
					glm::vec3 position = block->getPosition();
					int x = position.x;
					int y = position.y;
					int z = position.z;
					std::vector<Cube*> neighbors = {
							world->getBlock(x, y, z + 1),  // Face avant
							world->getBlock(x, y, z - 1),  // Face arrière
							world->getBlock(x - 1, y, z),  // Face gauche
							world->getBlock(x + 1, y, z),  // Face droite
							world->getBlock(x, y + 1, z),  // Face haut
							world->getBlock(x, y - 1, z)   // Face bas
					};
					block->render(neighbors);
				}
			}
		}
	}
	return waterBlocks;
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