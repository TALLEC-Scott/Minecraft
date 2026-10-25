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

#include "cube.h"

class ChunkManager;
class Cube;

void floodSkyLightWorld(ChunkManager* cm, int sx, int sy, int sz);
void floodBlockLight(ChunkManager* cm, int sx, int sy, int sz, uint8_t emission);
void removeBlockLightWorld(ChunkManager* cm, int sx, int sy, int sz);
// Block light after the cell at (x, y, z) changed from `oldType` to
// `newType` in place (World::setBlock — liquids flowing, draining and
// hardening): a lost emitter takes its light with it, a new one floods,
// and a cell turning opaque stops the light that passed through it.
void relightBlockChange(ChunkManager* cm, int x, int y, int z, block_type oldType, block_type newType);

// Intra-chunk block-light BFS: walks the chunk's blocks, seeds from any
// emissive block (glowstone etc.), floods outward. Writes the low nibble
// of the packed skyLight buffer; leaves the high (sky) nibble untouched.
// Cross-chunk propagation is handled separately by Chunk::propagateBorderLight.
void computeBlockLightData(Cube* blocks, uint8_t* skyLight, int maxSolidY);
