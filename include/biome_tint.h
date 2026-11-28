#pragma once

// Per-column grass and foliage colour multipliers (BiomeDef::grassTint /
// foliageTint, shifted by the column's own climate: climateTint in
// biome.h), box-blurred across biome borders so colours fade from one
// biome into the next instead of changing at a hard edge, like Minecraft's
// biome blend. Computed with the chunk on a worker thread (GL-free), then
// uploaded into BiomeTintMap's world-space textures for frag.shd.

#include <cstdint>

#include "biome.h"
#include "cube.h"

// Multipliers are stored as bytes, m * TINT_SCALE, so 1.0 (stock colour) is
// 128 and the range is 0 .. ~2.
inline constexpr float TINT_SCALE = 128.0f;
// Columns averaged on each side: a (2R+1)² box, ~9 blocks of fade.
inline constexpr int TINT_BLEND_RADIUS = 4;

inline uint8_t encodeTint(float m) {
    float v = m * TINT_SCALE + 0.5f;
    return static_cast<uint8_t>(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
}

struct BiomeTints {
    // [x][z] then rgb.
    uint8_t grass[CHUNK_SIZE][CHUNK_SIZE][3];
    uint8_t foliage[CHUNK_SIZE][CHUNK_SIZE][3];
};

// One column's unblurred colours.
struct ColumnTint {
    glm::vec3 grass, foliage;
};

// The colours of a column with biome `b` and climate (t, h).
inline ColumnTint columnTint(Biome b, double temperature, double humidity) {
    return {climateTint(b, temperature, humidity, false), climateTint(b, temperature, humidity, true)};
}

// `tintAt(i, k)` -> ColumnTint must answer for chunk-local columns i, k in
// [-TINT_BLEND_RADIUS, CHUNK_SIZE + TINT_BLEND_RADIUS).
template <typename TintAt> void blendColumnTints(TintAt&& tintAt, BiomeTints& out) {
    constexpr int R = TINT_BLEND_RADIUS;
    constexpr int W = CHUNK_SIZE + 2 * R;
    // Rows first: sum over i of each window, for every k in the margin.
    static thread_local glm::vec3 rowG[CHUNK_SIZE][W], rowF[CHUNK_SIZE][W];
    static thread_local glm::vec3 g[W][W], f[W][W];
    for (int i = 0; i < W; i++)
        for (int k = 0; k < W; k++) {
            const ColumnTint c = tintAt(i - R, k - R);
            g[i][k] = c.grass;
            f[i][k] = c.foliage;
        }
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int k = 0; k < W; k++) {
            glm::vec3 sg(0.0f), sf(0.0f);
            for (int di = 0; di <= 2 * R; di++) {
                sg += g[x + di][k];
                sf += f[x + di][k];
            }
            rowG[x][k] = sg;
            rowF[x][k] = sf;
        }
    constexpr float INV = 1.0f / static_cast<float>((2 * R + 1) * (2 * R + 1));
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            glm::vec3 sg(0.0f), sf(0.0f);
            for (int dk = 0; dk <= 2 * R; dk++) {
                sg += rowG[x][z + dk];
                sf += rowF[x][z + dk];
            }
            for (int c = 0; c < 3; c++) {
                out.grass[x][z][c] = encodeTint(sg[c] * INV);
                out.foliage[x][z][c] = encodeTint(sf[c] * INV);
            }
        }
}

// The biomes' own tints only (no climate shift), blended the same way.
template <typename BiomeAt> void blendBiomeTints(BiomeAt&& biomeAt, BiomeTints& out) {
    blendColumnTints(
        [&](int i, int k) {
            const BiomeDef& d = biomeDef(biomeAt(i, k));
            return ColumnTint{d.grassTint, d.foliageTint};
        },
        out);
}
