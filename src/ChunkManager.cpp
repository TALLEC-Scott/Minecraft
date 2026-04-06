//
// Created by scott on 03/07/23.
//

#include "ChunkManager.h"
#include <cmath>
#include "world.h"
#include <initializer_list>
#include <algorithm>

static constexpr int MAX_CHUNKS_PER_FRAME = 4;
static constexpr int MAX_INTEGRATE_PER_FRAME = 4;

ChunkManager::ChunkManager(int renderDist, int /*chunkSize*/, TerrainGenerator& terrainGenerator)
    : renderDistance(renderDist), terrainGenerator(terrainGenerator) {
    int diameter = 2 * renderDist + 1;
    chunks.reserve(static_cast<size_t>(diameter) * diameter * 2);

#ifndef __EMSCRIPTEN__
    int numWorkers = std::min(4u, std::max(1u, std::thread::hardware_concurrency() - 1));
    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(&ChunkManager::workerLoop, this);
#endif
}

ChunkManager::~ChunkManager() {
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        shutdownFlag = true;
    }
    requestCV.notify_all();
    for (auto& w : workers)
        w.join();
#endif
}

void ChunkManager::update(glm::vec3 cameraPosition) {
    int cx = (int)std::floor(cameraPosition.x / CHUNK_SIZE);
    int cz = (int)std::floor(cameraPosition.z / CHUNK_SIZE);
    glm::ivec2 currentChunk = glm::ivec2(cx, cz);

    glm::ivec2 minChunk = currentChunk - glm::ivec2(renderDistance);
    glm::ivec2 maxChunk = currentChunk + glm::ivec2(renderDistance);

#ifndef __EMSCRIPTEN__
    currentMin = minChunk;
    currentMax = maxChunk;
    drainResults();
#endif

    loadChunks(minChunk, maxChunk);
    unloadChunks(minChunk, maxChunk);
}

void ChunkManager::loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
#ifdef __EMSCRIPTEN__
    // Synchronous fallback
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
#else
    // Enqueue missing chunks for background generation
    for (int x = minChunk.x; x <= maxChunk.x; x++) {
        for (int z = minChunk.y; z <= maxChunk.y; z++) {
            glm::ivec2 pos(x, z);
            if (chunks.find(pos) == chunks.end() && pendingChunks.find(pos) == pendingChunks.end()) {
                pendingChunks.insert(pos);
                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    requestQueue.push(pos);
                }
                requestCV.notify_one();
            }
        }
    }
#endif
}

void ChunkManager::unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
    for (auto it = chunks.begin(); it != chunks.end();) {
        if (it->first.x < minChunk.x || it->first.x > maxChunk.x || it->first.y < minChunk.y ||
            it->first.y > maxChunk.y) {
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }

#ifndef __EMSCRIPTEN__
    // Also remove pending requests that are out of range
    for (auto it = pendingChunks.begin(); it != pendingChunks.end();) {
        if (it->x < minChunk.x || it->x > maxChunk.x || it->y < minChunk.y || it->y > maxChunk.y)
            it = pendingChunks.erase(it);
        else
            ++it;
    }
#endif
}

void ChunkManager::generateChunk(int x, int z) {
    Chunk chunk(x, z, terrainGenerator);
    chunks[glm::ivec2(x, z)] = std::move(chunk);
    for (auto& [dx, dz] : std::initializer_list<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
        auto it = chunks.find(glm::ivec2(x + dx, z + dz));
        if (it != chunks.end()) it->second.markDirty();
    }
}

Chunk* ChunkManager::getChunk(int chunkX, int chunkZ) {
    glm::ivec2 chunkPos = glm::ivec2(chunkX, chunkZ);
    auto it = chunks.find(chunkPos);
    if (it != chunks.end()) {
        return &(it->second);
    }
    return nullptr;
}

#ifndef __EMSCRIPTEN__
void ChunkManager::workerLoop() {
    while (true) {
        glm::ivec2 pos;
        {
            std::unique_lock<std::mutex> lock(requestMutex);
            requestCV.wait(lock, [&] { return !requestQueue.empty() || shutdownFlag.load(); });
            if (shutdownFlag.load() && requestQueue.empty()) return;
            pos = requestQueue.front();
            requestQueue.pop();
        }

        ChunkData data = generateChunkData(pos.x, pos.y, terrainGenerator);

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push(std::move(data));
        }
    }
}

void ChunkManager::drainResults() {
    int integrated = 0;

    while (integrated < MAX_INTEGRATE_PER_FRAME) {
        ChunkData data;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (resultQueue.empty()) break;
            data = std::move(resultQueue.front());
            resultQueue.pop();
        }

        glm::ivec2 pos(data.chunkX, data.chunkZ);
        pendingChunks.erase(pos);

        // Discard if chunk is no longer in render range
        if (pos.x < currentMin.x || pos.x > currentMax.x || pos.y < currentMin.y || pos.y > currentMax.y) {
            continue;
        }

        chunks[pos] = Chunk(std::move(data));

        // Invalidate neighbors for border face culling
        for (auto& [dx, dz] : std::initializer_list<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            auto it = chunks.find(glm::ivec2(pos.x + dx, pos.y + dz));
            if (it != chunks.end()) it->second.markDirty();
        }

        integrated++;
    }
}
#endif
