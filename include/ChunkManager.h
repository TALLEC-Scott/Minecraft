//
// Created by scott on 03/07/23.
//
#pragma once

#include <glm/vec3.hpp>
#include "chunk.h"
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
        std::hash<int> hasher;
        std::size_t seed = hasher(vec.x);
        seed ^= hasher(vec.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
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

  private:
    int renderDistance;
    TerrainGenerator& terrainGenerator;

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
        std::shared_ptr<uint8_t[]> skyLight;
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

    glm::ivec2 currentMin, currentMax;

    void workerLoop();
    void drainResults();
    void queueMeshBuild(glm::ivec2 pos);
    void queueDirtyMeshBuilds();
#endif
};
