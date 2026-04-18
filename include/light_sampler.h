#pragma once

// Unified packed-light sampler. Reads a single (x, y, z) voxel's packed light
// byte (high nibble = sky, low nibble = block) and transparently handles the
// cases that have historically been the source of chunk-edge lighting bugs:
//
//   - in-bounds: from the chunk's own flat skyLight buffer
//   - x/z out of bounds: from a pre-captured NeighborBorders snapshot, or
//                        from a live adjacent Chunk* (both variants provided)
//   - y out of bounds: open-sky default (sky=15, block=0)
//
// Every mesh builder should route through this helper instead of reimplementing
// its own OOB fallback logic — that duplication is how the same "face
// on chunk edge renders dark" bug has appeared twice now.

#include "light_data.h"
#include "mesh_types.h"

// Sample from a NeighborBorders snapshot (async mesh path).
// `light` is the current chunk's packed CHUNK_SIZE × CHUNK_HEIGHT × CHUNK_SIZE flat array.
inline uint8_t sampleLightBorder(const uint8_t* light, const NeighborBorders& nb, int x, int y, int z) {
    if (y < 0 || y >= CHUNK_HEIGHT) return packLight(15, 0);
    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
        return light[lightIdx(x, y, z)];
    }
    if (x < 0) return nb.xNeg.valid ? nb.xNeg.lightBorder[z][y] : packLight(15, 0);
    if (x >= CHUNK_SIZE) return nb.xPos.valid ? nb.xPos.lightBorder[z][y] : packLight(15, 0);
    if (z < 0) return nb.zNeg.valid ? nb.zNeg.lightBorder[x][y] : packLight(15, 0);
    if (z >= CHUNK_SIZE) return nb.zPos.valid ? nb.zPos.lightBorder[x][y] : packLight(15, 0);
    return packLight(15, 0);
}
