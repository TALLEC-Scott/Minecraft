#pragma once

// POD types shared between the online mesh builder (chunk.cpp, GL-aware)
// and the offline mesh builder (chunk_mesh.cpp, GL-free, testable).
// Declared at namespace scope so they're usable without pulling in
// class Chunk / GL headers.

#include <cstdint>
#include <vector>
#include "cube.h"

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

// Pre-built CPU-side mesh data (can be built on any thread).
struct MeshData {
    std::vector<uint8_t> verts;          // opaque vertices (PackedVertex)
    std::vector<uint8_t> waterVerts;     // water vertices (WaterVertex)
    std::vector<unsigned int> opaqueIdx;
    std::vector<unsigned int> waterIdx;
    bool ready = false;
};
