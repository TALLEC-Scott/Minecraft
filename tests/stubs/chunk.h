// Test-only minimal Chunk. The real include/chunk.h is too heavy for the
// test binary — it pulls in ChunkSection, GLAD, and a destructor that
// calls glDeleteVertexArrays. This stub provides just the surface the
// water simulator uses (getBlockType/setBlockType/getWaterLevel/
// setWaterLevel/markDirty), all inline. tests/stubs is on the include
// path before include/ for the tests target only.
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include "cube.h"

class Chunk {
  public:
    uint8_t types[CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE]{};
    uint8_t waterLevels[CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE]{};
    int chunkX = 0;
    int chunkZ = 0;
    bool dirty = false;
    uint8_t sectionDirty = 0xFF;
    uint8_t builtDirtyMask = 0;

    void markSectionDirty(int sy) {
        if (sy < 0 || sy >= 8) return;
        sectionDirty |= (1u << sy);
    }
    bool isMeshDirty() const { return sectionDirty != 0; }

    // Packed light array (high nibble = sky, low nibble = block). Mirrors
    // the real Chunk's shape so light_propagation.cpp can run against the
    // stub. ensureSkyLightFlat() lazily allocates on first access.
    std::unique_ptr<uint8_t[]> skyLight;
    void ensureSkyLightFlat() {
        if (!skyLight)
            skyLight =
                std::unique_ptr<uint8_t[]>(new uint8_t[CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE]());
    }

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
        int sy = y / 16;
        markSectionDirty(sy);
        if (y % 16 == 0) markSectionDirty(sy - 1);
        if (y % 16 == 15) markSectionDirty(sy + 1);
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
