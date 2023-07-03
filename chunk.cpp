#include "chunk.h"

Chunk::Chunk() {
	blocks = new Cube[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
	if (blocks != nullptr) {
		Texture grass = Texture("./Textures/grass.png");
		Texture stone = Texture("./Textures/stone.png");
		for (int i = 0; i < CHUNK_SIZE; i++) {
			for (int j = 0; j < CHUNK_SIZE; j++) {
				for (int k = 0; k < CHUNK_SIZE; k++) {
					blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k] = Cube(i, j, k);
					blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k].initialize();
				}
			}
		}
	}
}

Cube* Chunk::getBlock(int i, int j, int k) {
	if ((i < 0 || i >= CHUNK_SIZE) || (j < 0 || j >= CHUNK_SIZE) || (k < 0 || k >= CHUNK_SIZE)) {
		return nullptr;
	}
	return &blocks[i * CHUNK_SIZE + j * CHUNK_SIZE + k * CHUNK_SIZE];
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

void Chunk::destroy() {
	for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
		blocks[i].destroy();
	}
	delete[] blocks;
}

Chunk::~Chunk() {
	destroy();
}