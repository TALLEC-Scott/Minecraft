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
#include "chunk_section.h"
#include "light_data.h"
#include "mesh_types.h"
#include "chunk_mesh.h"
#include "shader.h"
#include "skylight.h"
#include "sparse_skylight.h"
#include "TerrainGenerator.h"

static constexpr int NUM_SECTIONS = CHUNK_HEIGHT / 16;

// CPU-side chunk data — no GL resources, safe to build on worker threads
struct ChunkData {
    std::unique_ptr<ChunkSection> sections[NUM_SECTIONS];
    std::shared_ptr<uint8_t[]> skyLight;    // packed: high nibble = sky, low nibble = block
    std::shared_ptr<uint8_t[]> waterLevels; // per-block water level (0=source, 1-7=flow)
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};
    int chunkX = 0, chunkZ = 0;
    int maxSolidY = 0;

    ChunkData() = default;
    ChunkData(ChunkData&& o) noexcept
        : skyLight(std::move(o.skyLight)), waterLevels(std::move(o.waterLevels)), chunkX(o.chunkX), chunkZ(o.chunkZ),
          maxSolidY(o.maxSolidY) {
        for (int i = 0; i < NUM_SECTIONS; i++) sections[i] = std::move(o.sections[i]);
        std::memcpy(heights, o.heights, sizeof(heights));
        std::memcpy(biomes, o.biomes, sizeof(biomes));
    }
    ChunkData& operator=(ChunkData&& o) noexcept {
        if (this != &o) {
            for (int i = 0; i < NUM_SECTIONS; i++) sections[i] = std::move(o.sections[i]);
            skyLight = std::move(o.skyLight);
            waterLevels = std::move(o.waterLevels);
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
        skyLight =
            std::shared_ptr<uint8_t[]>(new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]());
        chunkX = -1;
        chunkY = -1;
    }

    Chunk(int chunkX, int chunkY, TerrainGenerator& terrainGenerator);
    Chunk(ChunkData&& data);

    // Move only
    // Init order matches member declaration order to satisfy -Wreorder.
    Chunk(Chunk&& other) noexcept
        : skyLight(std::move(other.skyLight)), waterLevels(std::move(other.waterLevels)),
          sparseLight(std::move(other.sparseLight)), maxSolidY(other.maxSolidY), chunkX(other.chunkX),
          chunkY(other.chunkY), meshBuildInFlight(other.meshBuildInFlight), sectionDirty(other.sectionDirty),
          chunkVAO(other.chunkVAO), chunkVBO(other.chunkVBO), chunkEBO(other.chunkEBO),
          opaqueIndexCount(other.opaqueIndexCount), waterVAO(other.waterVAO), waterVBO(other.waterVBO),
          waterEBO(other.waterEBO), waterIndexCount(other.waterIndexCount), pendingMesh(std::move(other.pendingMesh)) {
        for (int i = 0; i < NUM_SECTIONS; i++) {
            sections[i] = std::move(other.sections[i]);
            sectionMeshes[i] = std::move(other.sectionMeshes[i]);
        }
        std::memcpy(heights, other.heights, sizeof(heights));
        other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
        other.waterVAO = other.waterVBO = other.waterEBO = 0;
    }

    Chunk& operator=(Chunk&& other) noexcept {
        if (this != &other) {
            if (chunkVAO) glDeleteVertexArrays(1, &chunkVAO);
            if (chunkVBO) glDeleteBuffers(1, &chunkVBO);
            if (chunkEBO) glDeleteBuffers(1, &chunkEBO);
            if (waterVAO) glDeleteVertexArrays(1, &waterVAO);
            if (waterVBO) glDeleteBuffers(1, &waterVBO);
            if (waterEBO) glDeleteBuffers(1, &waterEBO);

            for (int i = 0; i < NUM_SECTIONS; i++) {
                sections[i] = std::move(other.sections[i]);
                sectionMeshes[i] = std::move(other.sectionMeshes[i]);
            }
            skyLight = std::move(other.skyLight);
            waterLevels = std::move(other.waterLevels);
            sparseLight = std::move(other.sparseLight);
            chunkX = other.chunkX;
            chunkY = other.chunkY;
            chunkVAO = other.chunkVAO;
            chunkVBO = other.chunkVBO;
            chunkEBO = other.chunkEBO;
            waterVAO = other.waterVAO;
            waterVBO = other.waterVBO;
            waterEBO = other.waterEBO;
            opaqueIndexCount = other.opaqueIndexCount;
            waterIndexCount = other.waterIndexCount;
            sectionDirty = other.sectionDirty;
            maxSolidY = other.maxSolidY;
            pendingMesh = std::move(other.pendingMesh);
            meshBuildInFlight = other.meshBuildInFlight;
            std::memcpy(heights, other.heights, sizeof(heights));

            other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
            other.waterVAO = other.waterVBO = other.waterEBO = 0;
        }
        return *this;
    }

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // 8 neighbor chunks (4 cardinal + 4 diagonal) used for cross-chunk
    // mesh-building lookups. Diagonals are required so 4-chunk corner
    // water-height averaging produces matching values on all sides.
    struct NeighborChunks {
        Chunk* nxNeg = nullptr;
        Chunk* nxPos = nullptr;
        Chunk* nzNeg = nullptr;
        Chunk* nzPos = nullptr;
        Chunk* dNN = nullptr; // (-X,-Z)
        Chunk* dNP = nullptr; // (-X,+Z)
        Chunk* dPN = nullptr; // (+X,-Z)
        Chunk* dPP = nullptr; // (+X,+Z)
    };

    // Neighbor-border / mesh-data types live at namespace scope in
    // mesh_types.h so chunk_mesh.cpp can depend on them without pulling
    // in class Chunk. These aliases preserve the existing
    // `Chunk::NeighborBorders` call sites.
    using NeighborBorder = ::NeighborBorder;
    using DiagonalCorner = ::DiagonalCorner;
    using NeighborBorders = ::NeighborBorders;
    using MeshData = ::MeshData;

    static NeighborBorders snapshotBorders(const NeighborChunks& nc);

    // Per-section cached mesh — used for incremental rebuilds.
    struct SectionMesh {
        std::vector<uint8_t> verts;      // opaque (PackedVertex)
        std::vector<uint8_t> waterVerts; // water (WaterVertex)
        std::vector<unsigned int> opaqueIdx;
        std::vector<unsigned int> waterIdx;
    };

    void buildMesh(const NeighborChunks& nc);
    void buildMeshData(const NeighborChunks& nc);
    void buildMeshDataAsync(const NeighborBorders& borders);
    void uploadMesh();
    std::vector<Cube*> render(const Shader& shaderProgram, const NeighborChunks& nc);
    void renderWater(const Shader& shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);
    void markDirty() { sectionDirty = 0xFF; }
    void markSectionDirty(int sy) {
        if (sy < 0 || sy >= NUM_SECTIONS) return;
        sectionDirty |= (1u << sy);
        // If a build is in flight or pending upload, also record this
        // bit so uploadMesh can restore it — otherwise the upload's
        // builtDirtyMask clear wipes changes made after the snapshot.
        if (meshBuildInFlight || builtDirtyMask != 0) dirtyDuringBuild |= (1u << sy);
    }
    bool isMeshDirty() const { return sectionDirty != 0; }
    void destroy();
    void destroyBlock(int x, int y, int z);
    void placeBlock(int x, int y, int z, block_type type);

    void setPendingMesh(MeshData&& m) {
        pendingMesh = std::move(m);
        pendingMesh.ready = true;
        meshBuildInFlight = false;
        // The async mesh replaces the sync mesh, so invalidate
        // section caches — the next sync rebuild must repopulate them.
        sectionCachesPopulated = false;
    }

    int getLocalHeight(int x, int y);
    int getGlobalHeight(int x, int y);
    Cube* getBlock(int i, int j, int k);

    // Section-based block access (preferred over getBlock)
    block_type getBlockType(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return AIR;
        int sy = y / 16;
        if (!sections[sy]) return AIR;
        return sections[sy]->getBlock(x, y % 16, z);
    }

    void setBlockType(int x, int y, int z, block_type t) {
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        int sy = y / 16;
        if (!sections[sy]) {
            if (t == AIR) return;
            sections[sy] = std::make_unique<ChunkSection>();
        }
        sections[sy]->setBlock(x, y % 16, z, t);
        // Extend maxSolidY so the mesh builder's iteration range includes
        // this block. Without this, water/blocks placed above the terrain
        // height (e.g., falling water columns) would be outside the face
        // loop's Y range and never get meshed.
        if (t != AIR && y > maxSolidY) maxSolidY = y;
        markSectionDirty(sy);
        if (y % 16 == 0) markSectionDirty(sy - 1);
        if (y % 16 == 15) markSectionDirty(sy + 1);
    }

    // Decompress all sections into a flat Cube[] buffer (for BFS/mesh algorithms)
    // Layout: blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z]
    std::shared_ptr<Cube[]> decompressBlocks() const {
        auto buf = std::shared_ptr<Cube[]>(new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]);
        for (int s = 0; s < NUM_SECTIONS; s++) {
            if (!sections[s]) continue; // default Cube() is AIR
            int baseY = s * 16;
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int ly = 0; ly < 16; ly++)
                    for (int z = 0; z < CHUNK_SIZE; z++) {
                        int y = baseY + ly;
                        size_t idx = static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z;
                        buf[idx].setType(sections[s]->getBlock(x, ly, z));
                    }
        }
        return buf;
    }

    ~Chunk();

    uint8_t getSkyLight(int x, int y, int z) const;
    uint8_t getBlockLight(int x, int y, int z) const;
    void propagateBorderLight(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos);

    // Section-based block storage — null sections mean all-air
    std::unique_ptr<ChunkSection> sections[NUM_SECTIONS];
    std::shared_ptr<uint8_t[]> skyLight;    // packed: high nibble = sky, low nibble = block
    std::shared_ptr<uint8_t[]> waterLevels; // per-block water level (0=source, 1-7=flow)
    // Permanent sparse storage for light values (~8-12 KB per chunk vs
    // flat 32 KB). Authoritative: skyLight flat array is a transient
    // decompression used only during mesh builds and BFS operations,
    // dropped by compressSkyLight() after uploadMesh.
    SparseSkyLight sparseLight;
    int maxSolidY = 0;

    // Compress flat skyLight into sparseLight and free the 32 KB buffer.
    // Called after uploadMesh to reclaim memory; skyLight is decompressed
    // again on the next access via ensureSkyLightFlat().
    void compressSkyLight();
    // Allocate skyLight and populate from sparseLight. No-op if already flat.
    void ensureSkyLightFlat();
    // Estimated CPU-side memory in bytes (sections + skyLight + waterLevels
    // + cached meshes). Used for profiling/benchmark output.
    size_t memoryUsage() const;
    // Per-component breakdown (bytes). Useful for profiling bottlenecks.
    struct MemBreakdown {
        size_t sections = 0;
        size_t skyLight = 0;
        size_t waterLevels = 0;
        size_t meshCache = 0;
        size_t pendingMesh = 0;
    };
    MemBreakdown memoryBreakdown() const;

    uint8_t getWaterLevel(int x, int y, int z) const {
        if (!waterLevels || x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE)
            return 0;
        return waterLevels[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE +
                           z];
    }

    void setWaterLevel(int x, int y, int z, uint8_t level) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
        if (!waterLevels) {
            waterLevels =
                std::shared_ptr<uint8_t[]>(new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]());
        }
        waterLevels[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z] =
            level;
    }
    int chunkX = -1;
    int chunkY = -1;
    bool meshBuildInFlight = false;
    // Frames since render() was last called. Used by ChunkManager to
    // flush pendingMesh to GPU for chunks that went invisible (otherwise
    // their ~hundreds-of-KB mesh data would sit in CPU memory forever).
    int framesSinceRender = 0;
    bool hasPendingMesh() const { return pendingMesh.ready; }
    // Per-section dirty bitmask: bit i is set when section i needs a
    // mesh rebuild. Starts 0xFF (all dirty) so the initial full build
    // happens. Cleared to 0 after uploadMesh. Block edits set only the
    // affected bit(s) via markSectionDirty.
    uint8_t sectionDirty = 0xFF;
    bool sectionCachesPopulated = false; // set after first sync full rebuild
    // Which dirty bits the in-flight / pending mesh was built from.
    // uploadMesh clears only these bits so new dirty bits set during
    // the async build (or between buildMeshData and uploadMesh) survive.
    uint8_t builtDirtyMask = 0;
    uint8_t dirtyDuringBuild = 0;
    // Cached per-section mesh data. Clean sections are reused across
    // incremental rebuilds; only dirty sections get re-meshed.
    SectionMesh sectionMeshes[NUM_SECTIONS];

  private:
    void computeSkyLight();
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE]{};

    // Chunk-level GPU mesh — opaque geometry
    GLuint chunkVAO = 0;
    GLuint chunkVBO = 0;
    GLuint chunkEBO = 0;
    int opaqueIndexCount = 0;
    // Water geometry — separate VBO with float32 positions for sub-block precision
    GLuint waterVAO = 0;
    GLuint waterVBO = 0;
    GLuint waterEBO = 0;
    int waterIndexCount = 0;
    MeshData pendingMesh;
};

// buildMeshFromData is declared in chunk_mesh.h (included above).
