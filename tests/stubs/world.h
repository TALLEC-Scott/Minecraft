// Test-only minimal world.h. Satisfies the two things WaterSimulator.cpp
// touches: World (chunkManager + setBlock) and WorldResolver (chunk-cached
// local-coord lookup). The real include/world.h drags in ChunkManager,
// TerrainGenerator, Shader, and a rendering pipeline; we don't need any of
// that here. Placed in tests/stubs/ which is on the include path ahead of
// include/ for the tests target.
#pragma once

#include <climits>
#include "chunk.h"
#include "ChunkManager.h"
#include "cube.h"
#include "WaterSimulator.h"

struct WorldResolver {
    ChunkManager* cm;
    int cachedCX = INT_MIN;
    int cachedCZ = INT_MIN;
    Chunk* cachedChunk = nullptr;

    explicit WorldResolver(ChunkManager* cm) : cm(cm) {}

    struct Local {
        Chunk* chunk;
        int lx;
        int lz;
    };

    Local local(int wx, int wz) {
        int cx = worldToChunk(wx), cz = worldToChunk(wz);
        if (cx != cachedCX || cz != cachedCZ) {
            cachedCX = cx;
            cachedCZ = cz;
            cachedChunk = cm->getChunk(cx, cz);
        }
        return {cachedChunk, worldToLocal(wx, cx), worldToLocal(wz, cz)};
    }
};

class World {
  public:
    ChunkManager* chunkManager;

    World() : chunkManager(new ChunkManager()) {}
    ~World() { delete chunkManager; }

    void setBlock(int x, int y, int z, block_type type, uint8_t waterLevel = 0) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        int cx = worldToChunk(x), cz = worldToChunk(z);
        Chunk* chunk = chunkManager->getChunk(cx, cz);
        if (!chunk) return;
        int lx = worldToLocal(x, cx), lz = worldToLocal(z, cz);
        chunk->setBlockType(lx, y, lz, type);
        chunk->setWaterLevel(lx, y, lz, waterLevel);
        chunk->markDirty();
    }
};
