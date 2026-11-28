#pragma once

// Vines (Minecraft's vine): a flat sheet of leaves lying against the side
// of a block, 1/16 block off its face. Like torches they are BF_CROSS
// blocks (no collision, no sky shadow, no AO, neighbour faces stay drawn)
// whose shape lives in the cell's state byte (the byte liquids use for
// their level, saved with the chunk):
//   bit 0 +X, bit 1 -X, bit 2 +Z, bit 3 -Z = the cell sides it covers.
// One cell may cover several sides (a vine in an inside corner). A vine
// hangs either against the block on that side (a leaf, a log) or, in a
// chain below such a vine, in the same plane under the vine above it (as
// Minecraft's vines hang down from leaves): its sides then face air.
// Pure (no GL, no World) so both meshers, world edits and tests share it.

#include <cstdint>

#include "cube.h"

namespace vine {

inline constexpr uint8_t POS_X = 1, NEG_X = 2, POS_Z = 4, NEG_Z = 8;
inline constexpr uint8_t ALL_SIDES = 15;
inline constexpr uint8_t INVALID = 0;
// Offset to the side each bit covers.
inline constexpr int SIDE[4][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
// Mesh normal index (chunk.cpp FACE_NORMALS / vert.shd NORMALS) of the
// quad on each side: it faces into the cell, away from the block it lies on.
inline constexpr uint8_t NORMAL_IDX[4] = {2, 3, 1, 0};
// How far the quad sits off the face it covers (vert.shd pushes it along
// its normal; packed vertex positions only resolve half blocks).
inline constexpr float INSET = 1.0f / 16.0f;

inline bool validState(uint8_t s) {
    return s != 0 && (s & ~ALL_SIDES) == 0;
}

// Side bit for a vine placed against the face whose outward normal is
// (nx, ny, nz) (placement cell minus clicked block): it covers the side
// toward the clicked block. Tops and bottoms can't hold one (INVALID).
inline uint8_t stateForFace(int nx, int ny, int nz) {
    if (ny != 0) return INVALID;
    for (int b = 0; b < 4; ++b)
        if (SIDE[b][0] == -nx && SIDE[b][2] == -nz) return static_cast<uint8_t>(1u << b);
    return INVALID;
}

// Blocks a vine can lie against: solid ones and leaves.
inline bool canSupport(block_type t) {
    return hasFlag(t, BF_SOLID);
}

// Corners of the quad covering side bit `b` (0..3) of the cell centred on
// (wx, wy, wz), on that side's face plane (the shader insets it), vertex
// order bottom, top, top, bottom; u runs along the face.
inline void quadCorners(float wx, float wy, float wz, int b, float p[4][3]) {
    const float sx = static_cast<float>(SIDE[b][0]) * 0.5f, sz = static_cast<float>(SIDE[b][2]) * 0.5f;
    // Along the face: z for an X side, x for a Z side.
    const float ax = sz != 0.0f ? 0.5f : 0.0f, az = sx != 0.0f ? 0.5f : 0.0f;
    const float y0 = wy - 0.5f, y1 = wy + 0.5f;
    const float c[4][3] = {{wx + sx - ax, y0, wz + sz - az},
                           {wx + sx - ax, y1, wz + sz - az},
                           {wx + sx + ax, y1, wz + sz + az},
                           {wx + sx + ax, y0, wz + sz + az}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) p[i][k] = c[i][k];
}

} // namespace vine
