#include "world.h"

World::World() {
    this->terrainGenerator = new TerrainGenerator(0, 0.1, 0, CHUNK_SIZE + 1);
    for (int i = 0; i < RENDER_DISTANCE; i++) {
        for (int j = 0; j < RENDER_DISTANCE; j++) {
            this->chunks[i * RENDER_DISTANCE + j] = new Chunk(i, j, *terrainGenerator);
            this->chunks[i * RENDER_DISTANCE + j]->translate(i, 0, j);
        }
    }
}

void World::render(Shader& shaderProgram) {
	for (int i = 0; i < RENDER_DISTANCE * RENDER_DISTANCE; i++) {
		chunks[i]->render(shaderProgram);
	}
}

void World::destroyBlock(glm::vec3 position) {
	int chunkX = (int)position.x / CHUNK_SIZE;
	int chunkZ = (int)position.z / CHUNK_SIZE;

	int x_chunk = (int)position.x % CHUNK_SIZE;
	int y_chunk = (int)position.y % CHUNK_SIZE;
	int z_chunk = (int)position.z % CHUNK_SIZE;

	chunks[chunkX * RENDER_DISTANCE + chunkZ]->destroyBlock(x_chunk, y_chunk, z_chunk);
}

World::~World() {
}