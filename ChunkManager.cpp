//
// Created by scott on 03/07/23.
//

#include "ChunkManager.h"
#include "world.h"

ChunkManager::ChunkManager(int renderDistance, int chunkSize, TerrainGenerator &terrainGenerator)
        : terrainGenerator(terrainGenerator) {};

void ChunkManager::update(glm::vec3 cameraPosition) {
        glm::ivec2 currentChunk = glm::ivec2(cameraPosition.x / CHUNK_SIZE, cameraPosition.z / CHUNK_SIZE);

        glm::ivec2 minChunk = currentChunk - glm::ivec2(RENDER_DISTANCE);
        glm::ivec2 maxChunk = currentChunk + glm::ivec2(RENDER_DISTANCE);

        loadChunks(minChunk, maxChunk);
        unloadChunks(minChunk, maxChunk);
    }

void ChunkManager::loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
    for (int x = minChunk.x; x <= maxChunk.x; x++) {
        for (int z = minChunk.y; z <= maxChunk.y; z++) {
            glm::ivec2 chunkPos = glm::ivec2(x, z);
            if (chunks.find(chunkPos) == chunks.end()) {
                chunks[chunkPos] = *generateChunk(x, z);
            }
        }
    }
}

    void ChunkManager::unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
    for (auto it = chunks.begin(); it != chunks.end(); ) {
            if (it->first.x < minChunk.x || it->first.x > maxChunk.x ||
                it->first.y < minChunk.y || it->first.y > maxChunk.y) {
                it = chunks.erase(it);
            } else {
                ++it;
            }
        }}


    Chunk* ChunkManager::generateChunk(int x, int z) {
        auto chunk = new Chunk(x, z, terrainGenerator);
        chunk->translate(x, 0, z);
        return chunk;
    }

Chunk *ChunkManager::getChunk(int chunkX, int chunkZ) {
    glm::ivec2 chunkPos = glm::ivec2(chunkX, chunkZ);
    auto it = chunks.find(chunkPos);
    if (it != chunks.end()) {
        return &(it->second);
    }
    return nullptr;
}
