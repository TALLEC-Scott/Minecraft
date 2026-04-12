#pragma once

// Pure (GL-free) mesh-building entry point and shared helpers.
// This is the function the async worker thread calls; the online
// path in Chunk::buildMeshData mirrors the same logic against live
// neighbor chunks. Keeping it in its own translation unit lets tests
// exercise the builder without needing an OpenGL context.

#include <utility>
#include "cube.h"
#include "mesh_types.h"

// Minecraft-style water surface height as a fraction of the block.
// Source blocks render at 15/16 ("last pixel missing"); flowing water
// scales down by 2/16 per level; falling water renders full-height so
// the column looks solid.
inline float waterHeightFromRaw(uint8_t raw) {
    if (waterIsFalling(raw)) return 15.0f / 16.0f;
    return (15.0f - waterFlowLevel(raw) * 2.0f) / 16.0f;
}

// Per-cell water surface height, templated on a cell sampler. Returns -1
// when the cell is out of bounds or not water. Water with water above
// is treated as full-block-height for flush column/pool junctions.
template <typename Sampler> float waterCellHeightT(int bx, int by, int bz, Sampler sample) {
    if (by < 0 || by >= CHUNK_HEIGHT) return -1.0f;
    WaterCellSample s = sample(bx, by, bz);
    if (!s.isWater()) return -1.0f;
    if (by + 1 < CHUNK_HEIGHT && sample(bx, by + 1, bz).isWater()) return 1.0f;
    return waterHeightFromRaw(s.raw);
}

// Four top-face corner heights for a water block, averaging up to four
// cells meeting at each corner. Corner offsets depend on face axis signs
// so they match the quad's vertex order (SW, NW, NE, SE when u/v > 0).
// AIR cells contribute 0 (cliff-edge slant); SOLID cells are excluded
// (water against land stays flat). Any corner whose contributing cells
// include a "full-height" water (water above) snaps to the block ceiling.
template <typename Sampler>
void computeWaterTopCornersT(int bx, int by, int bz, int uSign, int vSign, float out[4], Sampler sample) {
    int cdx[4] = {-1, -1, 1, 1};
    int cdz[4] = {-1, 1, 1, -1};
    if (uSign < 0) {
        std::swap(cdx[0], cdx[3]);
        std::swap(cdx[1], cdx[2]);
        std::swap(cdz[0], cdz[3]);
        std::swap(cdz[1], cdz[2]);
    }
    if (vSign < 0) {
        std::swap(cdx[0], cdx[1]);
        std::swap(cdx[2], cdx[3]);
        std::swap(cdz[0], cdz[1]);
        std::swap(cdz[2], cdz[3]);
    }
    auto contribute = [&](int x, int y, int z) -> std::pair<float, bool> {
        if (y < 0 || y >= CHUNK_HEIGHT) return {0.0f, false};
        WaterCellSample s = sample(x, y, z);
        if (s.kind == CellKind::Solid) return {0.0f, false};
        if (s.kind == CellKind::Air) return {0.0f, true};
        return {waterHeightFromRaw(s.raw), true};
    };
    auto fullHeight = [&](int x, int y, int z) -> bool {
        if (y < 0 || y + 1 >= CHUNK_HEIGHT) return false;
        return sample(x, y, z).isWater() && sample(x, y + 1, z).isWater();
    };
    auto cC = contribute(bx, by, bz);
    for (int ci = 0; ci < 4; ci++) {
        bool anyFull = fullHeight(bx, by, bz) || fullHeight(bx + cdx[ci], by, bz) || fullHeight(bx, by, bz + cdz[ci]) ||
                       fullHeight(bx + cdx[ci], by, bz + cdz[ci]);
        if (anyFull) {
            out[ci] = (float)by + 0.5f;
            continue;
        }
        std::pair<float, bool> h[4] = {cC, contribute(bx + cdx[ci], by, bz), contribute(bx, by, bz + cdz[ci]),
                                       contribute(bx + cdx[ci], by, bz + cdz[ci])};
        float sum = 0;
        int cnt = 0;
        for (int i = 0; i < 4; i++)
            if (h[i].second) {
                sum += h[i].first;
                cnt++;
            }
        out[ci] = (float)by - 0.5f + (cnt > 0 ? sum / cnt : 0.0f);
    }
}

// Thread-safe mesh build from raw chunk data. `light` is the packed
// sky/block light array, `waterLevels` is the per-block flow-level
// array (may be null if no water in this chunk), `nb` is a snapshot of
// the 4 cardinal + 4 diagonal neighbor borders. No GL calls.
MeshData buildMeshFromData(Cube* blocks, uint8_t* light, uint8_t* waterLevels, int maxSolidY, int chunkX, int chunkZ,
                           const NeighborBorders& nb);
