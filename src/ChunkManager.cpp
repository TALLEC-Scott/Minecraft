//
// Created by scott on 03/07/23.
//

#include "ChunkManager.h"
#include "world.h"
#include <initializer_list>

static constexpr int MAX_CHUNKS_PER_FRAME = 4;

ChunkManager::ChunkManager(int renderDistance, int chunkSize, TerrainGenerator &terrainGenerator)
        : terrainGenerator(terrainGenerator) {
    int diameter = 2 * renderDistance + 1;
    chunks.reserve(diameter * diameter * 2);
}

void ChunkManager::update(glm::vec3 cameraPosition) {
    glm::ivec2 currentChunk = glm::ivec2(cameraPosition.x / CHUNK_SIZE, cameraPosition.z / CHUNK_SIZE);

    glm::ivec2 minChunk = currentChunk - glm::ivec2(RENDER_DISTANCE);
    glm::ivec2 maxChunk = currentChunk + glm::ivec2(RENDER_DISTANCE);

    loadChunks(minChunk, maxChunk);
    unloadChunks(minChunk, maxChunk);
}

void ChunkManager::loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
    int generated = 0;
    for (int x = minChunk.x; x <= maxChunk.x && generated < MAX_CHUNKS_PER_FRAME; x++) {
        for (int z = minChunk.y; z <= maxChunk.y && generated < MAX_CHUNKS_PER_FRAME; z++) {
            glm::ivec2 chunkPos = glm::ivec2(x, z);
            if (chunks.find(chunkPos) == chunks.end()) {
                generateChunk(x, z);
                generated++;
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
    }
}

void ChunkManager::generateChunk(int x, int z) {
    Chunk chunk(x, z, terrainGenerator);
    chunks[glm::ivec2(x, z)] = std::move(chunk);
    // Invalidate neighbors so they rebuild without their now-internal border faces
    for (auto& [dx, dz] : std::initializer_list<std::pair<int,int>>{{-1,0},{1,0},{0,-1},{0,1}}) {
        auto it = chunks.find(glm::ivec2(x + dx, z + dz));
        if (it != chunks.end()) it->second.markDirty();
    }
}

Chunk *ChunkManager::getChunk(int chunkX, int chunkZ) {
    glm::ivec2 chunkPos = glm::ivec2(chunkX, chunkZ);
    auto it = chunks.find(chunkPos);
    if (it != chunks.end()) {
        return &(it->second);
    }
    return nullptr;
}
