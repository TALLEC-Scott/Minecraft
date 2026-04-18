#pragma once

// World-space BFS light propagation — crosses chunk boundaries via
// WorldResolver. Extracted from world.cpp so that the unit-test target
// (which can't link the real, GL-dependent Chunk / ChunkManager) can drive
// these functions through the stubs in tests/stubs/.
//
// Contract required of Chunk + ChunkManager:
//   Chunk:
//     void ensureSkyLightFlat();
//     std::unique_ptr<uint8_t[]> skyLight;          // packed nibbles: sky<<4 | block
//     void markSectionDirty(int sy);
//     block_type getBlockType(int x, int y, int z) const;
//   ChunkManager:
//     Chunk* getChunk(int cx, int cz);

#include <cstdint>

class ChunkManager;

void floodSkyLightWorld(ChunkManager* cm, int sx, int sy, int sz);
void floodBlockLight(ChunkManager* cm, int sx, int sy, int sz, uint8_t emission);
void removeBlockLightWorld(ChunkManager* cm, int sx, int sy, int sz);
