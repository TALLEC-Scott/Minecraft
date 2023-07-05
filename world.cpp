#include "world.h"

World::World() {
    this->terrainGenerator = new TerrainGenerator(0, 0.1, 0, CHUNK_SIZE + 1);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
}

void World::destroyBlock(glm::vec3 position) const {
	int chunkX = (int)position.x / CHUNK_SIZE;
	int chunkZ = (int)position.z / CHUNK_SIZE;

	int x_chunk = (int)position.x % CHUNK_SIZE;
	int y_chunk = (int)position.y % CHUNK_SIZE;
	int z_chunk = (int)position.z % CHUNK_SIZE;

    auto chunk = this->chunkManager->getChunk(chunkX, chunkZ);
    chunk->destroyBlock(x_chunk, y_chunk, z_chunk);
}

World::~World() {
    delete this->terrainGenerator;
    delete this->chunkManager;
}

Chunk* World::getChunk(int x, int y) {
    return this->chunkManager->getChunk(x, y);
}

Cube* World::getBlock(int x, int y, int z) const {
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
    auto res = this->chunkManager->getChunk(chunkX, chunkZ);
    if (res == nullptr)
        return nullptr;

    return res->getBlock(x_chunk, y_chunk, z_chunk);
}

void World::render(Shader shaderProgram) const {
    for (auto& pair : this->chunkManager->chunks) {
        Chunk& chunk = pair.second;
        std::vector<std::vector<Cube*>> water;
        water.push_back(chunk.render(shaderProgram));
        for (auto waterBlocks : water) {
            for (auto & waterBlock : waterBlocks) {
                waterBlock->render(shaderProgram);
            }
        }
    }
}

void World::update(glm::vec3 cameraPosition) const {
    this->chunkManager->update(cameraPosition);
}