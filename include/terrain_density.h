#pragma once

// 3D terrain density: overhangs, ledges, arches and notches around the
// heightmap surface (Minecraft's "the ground isn't a heightmap" look).
//
// The heightmap height h stays the target surface, so climate, biomes,
// coasts and rivers don't move. Near it, in a band of ±AMPLITUDE·jagged
// blocks, a block at y is solid when
//
//     density = (h + 0.5 − y) + AMPLITUDE · jagged · N(x, y, z)  >  0
//
// N is TerrainGenerator::overhangNoise, sampled on a world-aligned lattice
// (LATTICE blocks) and trilinearly interpolated — as Minecraft does —
// which keeps it cheap and gives the faceted, planar faces of its cliffs.
// `jagged` (0..1, TerrainGenerator::getShape) is 0 on plains, near water,
// on beaches / ocean and along rivers, so those columns are untouched; the
// band never reaches below sea + 2 nor above CEILING.
//
// Floating specks: solid pieces not connected to the ground whose
// footprint fits in FLOAT_MAX × FLOAT_MAX columns are removed. Columns are
// shaped in a window FLOAT_MAX wider than requested on every side, so any
// such piece touching the requested area is seen whole: the result for a
// column depends only on (seed, x, z), never on which chunk asks.

#include "TerrainGenerator.h"

#include <cstdint>

namespace terraindensity {

inline constexpr double AMPLITUDE = 18.0; // blocks the surface can move at jagged = 1
inline constexpr int LATTICE = 4;         // noise lattice spacing (blocks, all axes)
inline constexpr int FLOAT_MAX = 5;       // max footprint of a removed floating piece
inline constexpr int BAND_FLOOR = 2;      // the band starts at least this far above sea level
inline constexpr int CEILING = 114;       // no density-solid block above this

// One column's solid profile after shaping.
struct ColumnBand {
    int top = 0;      // topmost solid block
    int floor = 0;    // top of the run of solid blocks rising from below the band
    int lo = 0;       // band start: everything below is solid
    int count = 0;    // band length; 0 = plain column, solid exactly up to `top`
    uint64_t solid{}; // bit b: block lo + b is solid
    bool solidAt(int y) const {
        if (count == 0) return y <= top;
        if (y < lo) return true;
        if (y >= lo + count) return false;
        return ((solid >> (y - lo)) & 1) != 0;
    }
};

// Shapes the n × n columns starting at world (wx0, wz0). `height` and
// `jagged` hold those columns' TerrainGenerator shape, row-major by x
// (index (x − wx0) · n + (z − wz0)); `out` gets their bands, same layout.
// Columns of the FLOAT_MAX ring around the square are sampled from
// `terrain` only when some column of the square is jagged.
void shapeColumns(TerrainGenerator& terrain, int wx0, int wz0, int n, const int* height, const float* jagged,
                  ColumnBand* out);

// Surface (topmost terrain block) of each column of chunk (chunkX, chunkZ),
// as fillChunkTerrain reports it, without generating the chunk.
void surfaceHeights(TerrainGenerator& terrain, int chunkX, int chunkZ, int (&heights)[CHUNK_SIZE][CHUNK_SIZE]);

} // namespace terraindensity
