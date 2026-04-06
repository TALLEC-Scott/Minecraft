/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#pragma once

#include <cstring>
#include <vector>
#include <cstdint>
#include "gl_header.h"
#include <glm/glm.hpp>
#include "cube.h"
#include "shader.h"
#include "TerrainGenerator.h"

// CPU-side chunk data — no GL resources, safe to build on worker threads
struct ChunkData {
    Cube* blocks = nullptr;
    uint8_t* skyLight = nullptr;
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};
    int chunkX = 0, chunkZ = 0;
    int maxSolidY = 0;

    ChunkData() = default;
    ChunkData(ChunkData&& o) noexcept
        : blocks(o.blocks), skyLight(o.skyLight), chunkX(o.chunkX), chunkZ(o.chunkZ), maxSolidY(o.maxSolidY) {
        std::memcpy(heights, o.heights, sizeof(heights));
        std::memcpy(biomes, o.biomes, sizeof(biomes));
        o.blocks = nullptr;
        o.skyLight = nullptr;
    }
    ChunkData& operator=(ChunkData&& o) noexcept {
        if (this != &o) {
            delete[] blocks;
            delete[] skyLight;
            blocks = o.blocks;
            skyLight = o.skyLight;
            chunkX = o.chunkX;
            chunkZ = o.chunkZ;
            maxSolidY = o.maxSolidY;
            std::memcpy(heights, o.heights, sizeof(heights));
            std::memcpy(biomes, o.biomes, sizeof(biomes));
            o.blocks = nullptr;
            o.skyLight = nullptr;
        }
        return *this;
    }
    ChunkData(const ChunkData&) = delete;
    ChunkData& operator=(const ChunkData&) = delete;
    ~ChunkData() {
        delete[] blocks;
        delete[] skyLight;
    }
};

// Generate chunk data on any thread (no GL calls)
ChunkData generateChunkData(int chunkX, int chunkZ, TerrainGenerator& terrain);

class Chunk {
  public:
    Chunk() {
        blocks = new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE];
        skyLight = new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]();
        chunkX = -1;
        chunkY = -1;
    }

    Chunk(int chunkX, int chunkY, TerrainGenerator& terrainGenerator);
    Chunk(ChunkData&& data); // construct from pre-generated data (main thread)

    // Move only — Cube GL buffers (now chunk-level) must not be double-freed
    Chunk(Chunk&& other) noexcept
        : blocks(other.blocks), skyLight(other.skyLight), chunkX(other.chunkX), chunkY(other.chunkY),
          chunkVAO(other.chunkVAO), chunkVBO(other.chunkVBO), chunkEBO(other.chunkEBO),
          opaqueIndexCount(other.opaqueIndexCount), waterIndexCount(other.waterIndexCount),
          waterIndexOffset(other.waterIndexOffset), meshDirty(other.meshDirty), maxSolidY(other.maxSolidY),
          pendingMesh(std::move(other.pendingMesh)) {
        std::memcpy(heights, other.heights, sizeof(heights));
        other.blocks = nullptr;
        other.skyLight = nullptr;
        other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
    }

    Chunk& operator=(Chunk&& other) noexcept {
        if (this != &other) {
            delete[] blocks;
            if (chunkVAO) glDeleteVertexArrays(1, &chunkVAO);
            if (chunkVBO) glDeleteBuffers(1, &chunkVBO);
            if (chunkEBO) glDeleteBuffers(1, &chunkEBO);

            blocks = other.blocks;
            delete[] skyLight;
            skyLight = other.skyLight;
            chunkX = other.chunkX;
            chunkY = other.chunkY;
            chunkVAO = other.chunkVAO;
            chunkVBO = other.chunkVBO;
            chunkEBO = other.chunkEBO;
            opaqueIndexCount = other.opaqueIndexCount;
            waterIndexCount = other.waterIndexCount;
            waterIndexOffset = other.waterIndexOffset;
            meshDirty = other.meshDirty;
            maxSolidY = other.maxSolidY;
            pendingMesh = std::move(other.pendingMesh);
            std::memcpy(heights, other.heights, sizeof(heights));

            other.blocks = nullptr;
            other.skyLight = nullptr;
            other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
        }
        return *this;
    }

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // Pre-built CPU-side mesh data (can be built on any thread)
    struct MeshData {
        std::vector<float> verts;
        std::vector<unsigned int> opaqueIdx;
        std::vector<unsigned int> waterIdx;
        bool ready = false;
    };

    void buildMesh(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void buildMeshData(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void uploadMesh(); // GL upload only — call on main thread after buildMeshData
    std::vector<Cube*> render(const Shader& shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void renderWater(const Shader& shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void markDirty() { meshDirty = true; }
    void destroy();
    void destroyBlock(int x, int y, int z);

    int getLocalHeight(int x, int y);
    int getGlobalHeight(int x, int y);
    Cube* getBlock(int i, int j, int k);

    ~Chunk();

    // Sky light at local coords (0=full shadow, 15=full sky). Returns 15 for out-of-bounds.
    uint8_t getSkyLight(int x, int y, int z) const;

  private:
    void computeSkyLight();
    Cube* blocks = nullptr;
    uint8_t* skyLight = nullptr; // per-block sky light level (0-15)
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};
    int chunkX = -1;
    int chunkY = -1;

    // Chunk-level GPU mesh
    GLuint chunkVAO = 0;
    GLuint chunkVBO = 0;
    GLuint chunkEBO = 0;
    int opaqueIndexCount = 0;
    int waterIndexCount = 0;
    size_t waterIndexOffset = 0;
    bool meshDirty = true;
    int maxSolidY = 0; // highest non-AIR y in chunk, used to skip empty slices
    MeshData pendingMesh; // CPU-built mesh awaiting GL upload
};
