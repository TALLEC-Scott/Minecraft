#pragma once

// Torches (Minecraft's torch / wall_torch): a cross-quad block like the
// flowers that emits light 14. A torch stands on the block below it or
// hangs on a wall; which one is kept in the cell's state byte (the byte
// liquids use for their level; non-liquid cells have it free):
//   0 = standing, 1..4 = on the wall at +X, -X, +Z, -Z (the support).
// Torches need an opaque solid block to hang on and pop off when it goes.
// Pure (no GL, no World) so meshers, the world and tests share it.

#include <cstdint>

#include "cube.h"

namespace torch {

inline constexpr uint8_t STANDING = 0;
inline constexpr uint8_t INVALID = 0xFF;
// Supporting block offset per state.
inline constexpr int SUPPORT[5][3] = {{0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};

inline bool validState(uint8_t s) {
    return s <= 4;
}

// State for a torch placed against the face whose outward normal is
// (nx, ny, nz) (placement cell minus clicked block). Ceilings can't hold
// one (INVALID).
inline uint8_t stateForFace(int nx, int ny, int nz) {
    if (ny > 0) return STANDING;
    if (ny < 0) return INVALID;
    for (uint8_t s = 1; s <= 4; ++s)
        if (SUPPORT[s][0] == -nx && SUPPORT[s][2] == -nz) return s;
    return INVALID;
}

inline bool canSupport(block_type t) {
    return hasFlag(t, BF_SOLID) && hasFlag(t, BF_OPAQUE);
}

// Height of the torch's top (the flame) above the cell's bottom, from the
// texture: an 11-pixel stick.
inline constexpr float TOP = 11.0f / 16.0f;
// A wall torch leans its foot 0.5 toward the wall, so its top sits this
// far from the cell center toward the wall.
inline constexpr float WALL_TOP_SHIFT = 0.5f * (1.0f - TOP);

} // namespace torch

// Corners of the two diagonal planes of a cross-quad block at (wx, wy, wz)
// (block-centered), vertex order bottom, top, top, bottom. A wall torch's
// bottom corners shift half a block toward its wall, leaning it like
// Minecraft's wall torch (0.5 is what the ×2 vertex encoding can hold).
inline void crossQuadCorners(block_type t, uint8_t state, float wx, float wy, float wz, float a[4][3],
                             float b[4][3]) {
    float y0 = wy - 0.5f, y1 = wy + 0.5f;
    float dx = 0.0f, dz = 0.0f;
    if (t == TORCH && state != torch::STANDING && torch::validState(state)) {
        dx = 0.5f * static_cast<float>(torch::SUPPORT[state][0]);
        dz = 0.5f * static_cast<float>(torch::SUPPORT[state][2]);
    }
    const float pa[4][3] = {{wx - 0.5f + dx, y0, wz - 0.5f + dz},
                            {wx - 0.5f, y1, wz - 0.5f},
                            {wx + 0.5f, y1, wz + 0.5f},
                            {wx + 0.5f + dx, y0, wz + 0.5f + dz}};
    const float pb[4][3] = {{wx - 0.5f + dx, y0, wz + 0.5f + dz},
                            {wx - 0.5f, y1, wz + 0.5f},
                            {wx + 0.5f, y1, wz - 0.5f},
                            {wx + 0.5f + dx, y0, wz - 0.5f + dz}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) {
            a[i][k] = pa[i][k];
            b[i][k] = pb[i][k];
        }
}
