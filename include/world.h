#pragma once

#include <climits>
#include <glm/glm.hpp>
#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"
#include "ChunkManager.h"
#include "WaterSimulator.h"

// Cached world-coordinate resolver: given (wx, wy, wz), look up the
// owning Chunk and local coordinates. Holds the last chunk pointer so
// consecutive queries within the same chunk skip the hash-map lookup.
// Used by light BFS, water simulation, and anything else that walks
// neighbors across chunk boundaries.
struct WorldResolver {
    ChunkManager* cm;
    int cachedCX = INT_MIN, cachedCZ = INT_MIN;
    Chunk* cachedChunk = nullptr;

    explicit WorldResolver(ChunkManager* cm) : cm(cm) {}

    struct Local {
        Chunk* chunk;
        int lx;
        int lz;
    };

    // Local-coordinate resolution; caller handles y-bounds.
    Local local(int wx, int wz) {
        int cx = worldToChunk(wx), cz = worldToChunk(wz);
        if (cx != cachedCX || cz != cachedCZ) {
            cachedCX = cx;
            cachedCZ = cz;
            cachedChunk = cm->getChunk(cx, cz);
        }
        return {cachedChunk, worldToLocal(wx, cx), worldToLocal(wz, cz)};
    }

    // Flat index into the chunk's packed skyLight / cube arrays. Returns
    // {nullptr, 0} if out of bounds or the chunk has no skyLight array.
    std::pair<Chunk*, size_t> operator()(int wx, int wy, int wz) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return {nullptr, 0};
        auto loc = local(wx, wz);
        if (!loc.chunk || !loc.chunk->skyLight) return {nullptr, 0};
        return {loc.chunk,
                static_cast<size_t>(loc.lx) * CHUNK_HEIGHT * CHUNK_SIZE +
                    static_cast<size_t>(wy) * CHUNK_SIZE + loc.lz};
    }
};

class World {
  public:
    World(unsigned int seed = 0);

    // void render(Shader& shaderProgram);
    void destroyBlock(glm::vec3 position) const;
    void placeBlock(glm::ivec3 position, block_type type) const;
    void setBlock(int x, int y, int z, block_type type, uint8_t waterLevel = 0) const;
    Chunk* getChunk(int x, int y);
    Cube* getBlock(int x, int y, int z) const;
    TerrainGenerator* terrainGenerator;
    ChunkManager* chunkManager;
    WaterSimulator* waterSimulator;
    ~World();

    int render(const Shader& shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const;
    void update(glm::vec3 cameraPosition) const;
    // Raycast: returns true if a block was hit, sets hitPos to the block and prevPos to the air block before it
    bool raycast(glm::vec3 origin, glm::vec3 direction, float maxDist, glm::ivec3& hitPos, glm::ivec3& prevPos) const;
};