//
// Created by scott on 03/07/23.
//
#pragma once

#include <glm/vec3.hpp>
#include "chunk.h"
#include "world_save.h"
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#endif

struct Vec2Hash {
    std::size_t operator()(const glm::ivec2& vec) const {
        std::size_t seed = std::hash<int>()(vec.x);
        hashCombine(seed, vec.y);
        return seed;
    }
};

class World;
class Chunk;

class ChunkManager {

  public:
    ChunkManager(int renderDistance, int chunkSize, TerrainGenerator& terrainGenerator);
    ~ChunkManager();
    void update(glm::vec3 cameraPosition);
    void render(Shader shaderProgram);
    std::unordered_map<glm::ivec2, Chunk, Vec2Hash> chunks;
    Chunk* getChunk(int chunkX, int chunkZ);
    void setRenderDistance(int rd) { renderDistance = rd; }
    int getRenderDistance() const { return renderDistance; }
    void setWorldSave(WorldSave* ws) { worldSave = ws; }
    void saveAllModifiedChunks();
    // Sum of Chunk::memoryUsage across all loaded chunks, in bytes.
    size_t totalChunkMemory() const;
    Chunk::MemBreakdown totalChunkBreakdown() const;

  private:
    int renderDistance;
    TerrainGenerator& terrainGenerator;
    WorldSave* worldSave = nullptr;

    void loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);
    void unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);
    void generateChunk(int x, int z);

#ifndef __EMSCRIPTEN__
    // Chunk generation
    std::queue<glm::ivec2> requestQueue;
    std::mutex requestMutex;
    std::condition_variable requestCV;

    std::queue<ChunkData> resultQueue;
    std::mutex resultMutex;

    std::unordered_set<glm::ivec2, Vec2Hash> pendingChunks;

    // Mesh build requests (dispatched to workers with snapshotted data)
    struct MeshRequest {
        glm::ivec2 pos;
        std::shared_ptr<Cube[]> blocks;
        std::shared_ptr<uint8_t[]> skyLight; // packed: high nibble = sky, low nibble = block
        std::shared_ptr<uint8_t[]> waterLevels;
        int maxSolidY;
        int chunkX, chunkZ;
        Chunk::NeighborBorders borders;
    };

    struct MeshResult {
        glm::ivec2 pos;
        Chunk::MeshData mesh;
    };

    std::queue<MeshRequest> meshRequestQueue;
    std::queue<MeshResult> meshResultQueue; // guarded by resultMutex

    // Worker threads
    std::vector<std::thread> workers;
    std::atomic<bool> shutdownFlag{false};

    void workerLoop();
    void drainResults();
    void queueMeshBuild(glm::ivec2 pos);
    void queueDirtyMeshBuilds();
#endif

    // Render-distance box from the last update() call. Used to early-out
    // loadChunks/unloadChunks when the player hasn't crossed a chunk
    // boundary. Available on both desktop and web builds.
    glm::ivec2 currentMin{INT_MIN, INT_MIN};
    glm::ivec2 currentMax{INT_MIN, INT_MIN};
};
