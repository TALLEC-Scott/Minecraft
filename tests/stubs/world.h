// Test-only minimal world.h. Satisfies the two things WaterSimulator.cpp
// touches: World (chunkManager + setBlock) and WorldResolver (chunk-cached
// local-coord lookup). The real include/world.h drags in ChunkManager,
// TerrainGenerator, Shader, and a rendering pipeline; we don't need any of
// that here. Placed in tests/stubs/ which is on the include path ahead of
// include/ for the tests target.
#pragma once

#include "chunk.h"
#include "ChunkManager.h"
#include "cube.h"
#include "WaterSimulator.h"
#include "world_resolver.h"

using WorldResolver = WorldResolverT<ChunkManager, Chunk>;

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
