#pragma once

// POD types shared between the online mesh builder (chunk.cpp, GL-aware)
// and the offline mesh builder (chunk_mesh.cpp, GL-free, testable).
// Declared at namespace scope so they're usable without pulling in
// class Chunk / GL headers.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "block_layers.h"
#include "cube.h"
#include "section_visibility.h"

#pragma pack(push, 1)
struct PackedVertex {
    int16_t px, py, pz;
    uint8_t u, v;
    uint8_t normalIdx, texLayer, ao, packedLight;
};
#pragma pack(pop)
static_assert(sizeof(PackedVertex) == 12);

// Water vertices use float32 positions for sub-block precision (thin
// water edges at level 7 = 0.11 blocks). Same ×2 encoding as PackedVertex
// so the vertex shader's ×0.5 undo still works. Padded to 20 bytes so
// the stride is a multiple of 4 — WebGL requires that for GL_FLOAT.
#pragma pack(push, 1)
struct WaterVertex {
    float px, py, pz;
    uint8_t u, v;
    uint8_t normalIdx, texLayer, ao, packedLight;
    uint8_t _pad[2];
};
#pragma pack(pop)
static_assert(sizeof(WaterVertex) == 20);

// Lava shares the liquid mesh with water but draws opaque. Within one
// draw, triangles rasterize in index order, so putting lava first lets it
// write depth before the blended water: water in front of lava then
// blends over it instead of hiding it. Reorders idx[begin, end), whose
// values minus `vertBase` index WaterVertex records in `verts`.
inline void lavaTrianglesFirst(std::vector<unsigned int>& idx, size_t begin, size_t end, const uint8_t* verts,
                               unsigned int vertBase = 0) {
    auto isLava = [&](unsigned int v) {
        return reinterpret_cast<const WaterVertex*>(verts + static_cast<size_t>(v - vertBase) * sizeof(WaterVertex))
                   ->texLayer == block_layers::LAVA_LAYER;
    };
    bool anyLava = false, anyWater = false;
    for (size_t i = begin; i + 2 < end; i += 3) (isLava(idx[i]) ? anyLava : anyWater) = true;
    if (!anyLava || !anyWater) return;
    std::vector<std::array<unsigned int, 3>> tris;
    tris.reserve((end - begin) / 3);
    for (size_t i = begin; i + 2 < end; i += 3) tris.push_back({idx[i], idx[i + 1], idx[i + 2]});
    std::stable_partition(tris.begin(), tris.end(), [&](const auto& t) { return isLava(t[0]); });
    for (size_t t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) idx[begin + t * 3 + k] = tris[t][k];
}

// Tri-state so water corner averaging can distinguish AIR (cliff edges —
// pull corner down toward floor) from SOLID (shoreline — exclude from
// average so water against land stays flat).
enum class CellKind : uint8_t { Water, Air, Solid };

struct WaterCellSample {
    CellKind kind;
    uint8_t raw;
    bool isWater() const { return kind == CellKind::Water; }
};

// Snapshot of a neighbor chunk's boundary — captured sync, consumed
// async by the worker thread without touching the live neighbor.
struct NeighborBorder {
    block_type types[CHUNK_SIZE][CHUNK_HEIGHT]{};
    uint8_t lightBorder[CHUNK_SIZE][CHUNK_HEIGHT]{}; // high nibble = sky, low = block
    uint8_t waterBorder[CHUNK_SIZE][CHUNK_HEIGHT]{};
    bool valid = false;
};

// Diagonal neighbor's column at the shared corner — needed for
// 4-chunk intersection water-corner averaging.
struct DiagonalCorner {
    block_type types[CHUNK_HEIGHT]{};
    uint8_t waterBorder[CHUNK_HEIGHT]{};
    bool valid = false;
};

struct NeighborBorders {
    NeighborBorder xNeg, xPos, zNeg, zPos;
    DiagonalCorner dNN, dNP, dPN, dPP;
};

// Per-section index subranges within the chunk's concatenated buffers,
// recorded at stitch time so cave culling can draw a subset of sections.
// Starts are in index units relative to each region's own base (opaque
// region, cross region, water EBO respectively).
struct SectionDrawRanges {
    uint32_t opaqueStart = 0, opaqueCount = 0;
    uint32_t crossStart = 0, crossCount = 0;
    uint32_t waterStart = 0, waterCount = 0;
};

// Pre-built CPU-side mesh data (can be built on any thread).
struct MeshData {
    std::vector<uint8_t> verts;      // opaque vertices (PackedVertex)
    std::vector<uint8_t> waterVerts; // water vertices (WaterVertex)
    std::vector<unsigned int> opaqueIdx;
    // Cross-quad plant indices, appended to opaqueIdx in the EBO and drawn
    // with GL_CULL_FACE disabled so each plane renders from both sides with
    // only 6 indices (no reverse-winding duplicate).
    std::vector<unsigned int> crossIdx;
    std::vector<unsigned int> waterIdx;
    // Cave-culling metadata, filled alongside the geometry so it always
    // matches the mesh it ships with.
    SectionDrawRanges ranges[secvis::NUM_SECTIONS];
    uint16_t sectionConn[secvis::NUM_SECTIONS] = {secvis::CONN_ALL, secvis::CONN_ALL, secvis::CONN_ALL,
                                                  secvis::CONN_ALL, secvis::CONN_ALL, secvis::CONN_ALL,
                                                  secvis::CONN_ALL, secvis::CONN_ALL};
    bool ready = false;
};
