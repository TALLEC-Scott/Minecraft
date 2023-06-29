#include "chunk.h"

Chunk::Chunk() {
	blocks = (Cube*) malloc(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE * sizeof(Cube));
	if (blocks != NULL) {
		Texture grass = Texture("./Textures/grass.png");
		Texture stone = Texture("./Textures/stone.png");
		for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
			blocks[i] = Cube(0, 0, 0, grass);
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
				Cube block = blocks[i * CHUNK_SIZE + j * CHUNK_SIZE + k * CHUNK_SIZE];
				block.setPosition(glm::vec3(i, j, k));
				shaderProgram.setVec3("translation", block.getPosition());
				shaderProgram.use();
				// Vérifier les voisins et ne pas afficher les faces en contact
				Cube* neighbors[6] = {
					getBlock(i, j, k + 1),  // Face avant
					getBlock(i, j, k - 1),  // Face arrière
					getBlock(i - 1, j, k),  // Face gauche
					getBlock(i + 1, j, k),   // Face droite
					getBlock(i, j + 1, k),  // Face haut
					getBlock(i, j - 1, k)  // Face bas
				};

				block.render(neighbors);
			}
		}
	}
}

void Chunk::destroy() {
	for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
		blocks[i].destroy();
	}
	free(blocks);
}

Chunk::~Chunk() {
	for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
		blocks[i].destroy();
	}
	if (blocks != nullptr) {
		free(blocks);
	}
}