#pragma once

// GL-free world generation for one chunk: terrain layers, caves, ore,
// shores and vegetation, written into a raw block buffer. Split out of
// chunk.cpp's generateChunkData (which adds light + section compression)
// so it can be unit-tested and driven by offline analysis tools.

#include "TerrainGenerator.h"
#include "cube.h"

// Fills `blocks` (CHUNK_SIZE × CHUNK_HEIGHT × CHUNK_SIZE, indexed
// x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z, default-constructed
// to AIR) for chunk (chunkX, chunkZ), and reports each column's surface
// height and biome. Deterministic in (seed, chunkX, chunkZ); trees that
// straddle chunk borders come out identical from either side.
void fillChunkTerrain(TerrainGenerator& terrain, int chunkX, int chunkZ, Cube* blocks,
                      int (&heights)[CHUNK_SIZE][CHUNK_SIZE], Biome (&biomes)[CHUNK_SIZE][CHUNK_SIZE]);

namespace terrainfill {
// Surface-rule constants (blocks relative to sea level / neighbor steps),
// exposed for tests.
inline constexpr int CAVE_CRUST = 5;     // solid blocks kept above caves on land
inline constexpr int OCEAN_ROOF = 6;     // ... and under water
inline constexpr int SNOW_LINE = 34;     // surface snow at sea + this, any biome
inline constexpr int ROCK_LINE = 20;     // above sea + this, moderate slopes are bare rock
inline constexpr int CLIFF_SLOPE = 3;    // step to a neighbor that makes a cliff face
inline constexpr int TREE_CELL = 4;      // trees are placed one-per-cell on this world grid
} // namespace terrainfill
