#include "chunk.h"

Chunk::Chunk() {
	Texture grass = Texture("./Textures/grass.png");
	Texture stone = Texture("./Textures/stone.png");
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			Texture texture = stone;
			if (j > 10) {
				texture = grass;
			}
			for (int k = 0; k < CHUNK_SIZE; k++) {
				blocks.at(i).at(j).at(k) = Cube(i, j, k, texture);
			}
		}
	}
}

void Chunk::render(Shader shaderProgram) {
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
				glm::vec3 translation = glm::vec3(i, j, k);
				shaderProgram.setVec3("translation", translation);
				Cube block = blocks.at(i).at(j).at(k);
				block.render();
			}
		}
	}
}

void Chunk::destroy() {
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
				blocks.at(i).at(j).at(k).destroy();
			}
		}
	}
}

Chunk::~Chunk() {
	for (int i = 0; i < CHUNK_SIZE; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			for (int k = 0; k < CHUNK_SIZE; k++) {
				blocks.at(i).at(j).at(k).destroy();
			}
		}
	}
}