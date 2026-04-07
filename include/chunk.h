/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#pragma once

#include <cstring>
#include <memory>
#include <vector>
#include <cstdint>
#include "gl_header.h"
#include <glm/glm.hpp>
#include "cube.h"
#include "light_data.h"
#include "shader.h"
#include "TerrainGenerator.h"

// CPU-side chunk data — no GL resources, safe to build on worker threads
struct ChunkData {
    std::shared_ptr<Cube[]> blocks;
    std::shared_ptr<uint8_t[]> skyLight; // packed: high nibble = sky, low nibble = block
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};
    int chunkX = 0, chunkZ = 0;
    int maxSolidY = 0;

    ChunkData() = default;
    ChunkData(ChunkData&& o) noexcept
        : blocks(std::move(o.blocks)), skyLight(std::move(o.skyLight)),
          chunkX(o.chunkX), chunkZ(o.chunkZ),
          maxSolidY(o.maxSolidY) {
        std::memcpy(heights, o.heights, sizeof(heights));
        std::memcpy(biomes, o.biomes, sizeof(biomes));
    }
    ChunkData& operator=(ChunkData&& o) noexcept {
        if (this != &o) {
            blocks = std::move(o.blocks);
            skyLight = std::move(o.skyLight);
            chunkX = o.chunkX;
            chunkZ = o.chunkZ;
            maxSolidY = o.maxSolidY;
            std::memcpy(heights, o.heights, sizeof(heights));
            std::memcpy(biomes, o.biomes, sizeof(biomes));
        }
        return *this;
    }
    ChunkData(const ChunkData&) = delete;
    ChunkData& operator=(const ChunkData&) = delete;
    ~ChunkData() = default;
};

// Generate chunk data on any thread (no GL calls)
ChunkData generateChunkData(int chunkX, int chunkZ, TerrainGenerator& terrain);

class Chunk {
  public:
    Chunk() {
        blocks = std::shared_ptr<Cube[]>(new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]);
        skyLight =
            std::shared_ptr<uint8_t[]>(new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]());
        chunkX = -1;
        chunkY = -1;
    }

    Chunk(int chunkX, int chunkY, TerrainGenerator& terrainGenerator);
    Chunk(ChunkData&& data);

    // Move only
    Chunk(Chunk&& other) noexcept
        : blocks(std::move(other.blocks)), skyLight(std::move(other.skyLight)),
          chunkX(other.chunkX),
          chunkY(other.chunkY), chunkVAO(other.chunkVAO), chunkVBO(other.chunkVBO), chunkEBO(other.chunkEBO),
          opaqueIndexCount(other.opaqueIndexCount), waterIndexCount(other.waterIndexCount),
          waterIndexOffset(other.waterIndexOffset), meshDirty(other.meshDirty), maxSolidY(other.maxSolidY),
          pendingMesh(std::move(other.pendingMesh)), meshBuildInFlight(other.meshBuildInFlight) {
        std::memcpy(heights, other.heights, sizeof(heights));
        other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
    }

    Chunk& operator=(Chunk&& other) noexcept {
        if (this != &other) {
            if (chunkVAO) glDeleteVertexArrays(1, &chunkVAO);
            if (chunkVBO) glDeleteBuffers(1, &chunkVBO);
            if (chunkEBO) glDeleteBuffers(1, &chunkEBO);

            blocks = std::move(other.blocks);
            skyLight = std::move(other.skyLight);
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
            meshBuildInFlight = other.meshBuildInFlight;
            std::memcpy(heights, other.heights, sizeof(heights));

            other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
        }
        return *this;
    }

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // Snapshot of one neighbor's border blocks for async mesh building
    struct NeighborBorder {
        block_type types[CHUNK_SIZE][CHUNK_HEIGHT]{};
        uint8_t lightBorder[CHUNK_SIZE][CHUNK_HEIGHT]{}; // packed: high nibble = sky, low nibble = block
        bool valid = false;
    };

    struct NeighborBorders {
        NeighborBorder xNeg, xPos, zNeg, zPos;
    };

    static NeighborBorders snapshotBorders(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);

    // Pre-built CPU-side mesh data (can be built on any thread)
    struct MeshData {
        std::vector<float> verts;
        std::vector<unsigned int> opaqueIdx;
        std::vector<unsigned int> waterIdx;
        bool ready = false;
    };

    void buildMesh(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void buildMeshData(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void buildMeshDataAsync(const NeighborBorders& borders);
    void uploadMesh();
    std::vector<Cube*> render(const Shader& shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void renderWater(const Shader& shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void markDirty() { meshDirty = true; }
    void destroy();
    void destroyBlock(int x, int y, int z);
    void placeBlock(int x, int y, int z, block_type type);

    void setPendingMesh(MeshData&& m) {
        pendingMesh = std::move(m);
        pendingMesh.ready = true;
        meshBuildInFlight = false;
    }

    int getLocalHeight(int x, int y);
    int getGlobalHeight(int x, int y);
    Cube* getBlock(int i, int j, int k);

    ~Chunk();

    uint8_t getSkyLight(int x, int y, int z) const;
    uint8_t getBlockLight(int x, int y, int z) const;
    void propagateBorderLight(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);

    // Shared block data — safe to capture by worker threads
    std::shared_ptr<Cube[]> blocks;
    std::shared_ptr<uint8_t[]> skyLight; // packed: high nibble = sky, low nibble = block
    int maxSolidY = 0;
    int chunkX = -1;
    int chunkY = -1;
    bool meshBuildInFlight = false;
    bool meshDirty = true;

  private:
    void computeSkyLight();
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};

    // Chunk-level GPU mesh
    GLuint chunkVAO = 0;
    GLuint chunkVBO = 0;
    GLuint chunkEBO = 0;
    int opaqueIndexCount = 0;
    int waterIndexCount = 0;
    size_t waterIndexOffset = 0;
    MeshData pendingMesh;
};

// Build mesh from raw data — fully thread-safe, no GL calls
Chunk::MeshData buildMeshFromData(Cube* blocks, uint8_t* light, int maxSolidY, int chunkX,
                                  int chunkZ, const Chunk::NeighborBorders& borders);
