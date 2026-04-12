// Test-only minimal Chunk. The real include/chunk.h is too heavy for the
// test binary — it pulls in ChunkSection, GLAD, and a destructor that
// calls glDeleteVertexArrays. This stub provides just the surface the
// water simulator uses (getBlockType/setBlockType/getWaterLevel/
// setWaterLevel/markDirty), all inline. tests/stubs is on the include
// path before include/ for the tests target only.
#pragma once

#include <cstdint>
#include <cstring>
#include "cube.h"

class Chunk {
  public:
    uint8_t types[CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE]{};
    uint8_t waterLevels[CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE]{};
    int chunkX = 0;
    int chunkZ = 0;
    bool dirty = false;

    // skyLight is unused by the water sim but WorldResolver::operator()
    // checks it in the real codebase; keep a trivially-valid stub so both
    // code paths compile.
    bool skyLight = true;

    Chunk() = default;
    Chunk(int cx, int cz) : chunkX(cx), chunkZ(cz) {}

    static size_t idx(int x, int y, int z) {
        return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE +
               static_cast<size_t>(y) * CHUNK_SIZE + static_cast<size_t>(z);
    }

    block_type getBlockType(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return AIR;
        return static_cast<block_type>(types[idx(x, y, z)]);
    }

    void setBlockType(int x, int y, int z, block_type t) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
        types[idx(x, y, z)] = static_cast<uint8_t>(t);
    }

    uint8_t getWaterLevel(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
        return waterLevels[idx(x, y, z)];
    }

    void setWaterLevel(int x, int y, int z, uint8_t level) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
        waterLevels[idx(x, y, z)] = level;
    }

    void markDirty() { dirty = true; }
};
