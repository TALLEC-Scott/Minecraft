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
		chunks[i]->render(shaderProgram, this);
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

Chunk* World::getChunk(int x, int y) {
    return chunks[x * RENDER_DISTANCE + y];
}

Cube* World::getBlock(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_SIZE)
        return nullptr;
    if (x < 0 || x >= CHUNK_SIZE * RENDER_DISTANCE || z < 0 || z >= CHUNK_SIZE * RENDER_DISTANCE)
        return nullptr;

    int chunkX = x / CHUNK_SIZE;
    int chunkZ = z / CHUNK_SIZE;
    if (chunkX < 0 || chunkX >= RENDER_DISTANCE || chunkZ < 0 || chunkZ >= RENDER_DISTANCE)
        return nullptr;
    int x_chunk = x % CHUNK_SIZE;
    int y_chunk = y % CHUNK_SIZE;
    int z_chunk = z % CHUNK_SIZE;
    auto res = chunks[chunkX * RENDER_DISTANCE + chunkZ];
    if (res == nullptr)
        return nullptr;

    return chunks[chunkX * RENDER_DISTANCE + chunkZ]->getBlock(x_chunk, y_chunk, z_chunk);
}

