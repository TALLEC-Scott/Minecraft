// Tests for the offline mesh builder (buildMeshFromData, src/chunk_mesh.cpp).
//
// buildMeshFromData is the pure, thread-safe entry point used by the
// async worker thread. It takes raw Cube/light/waterLevel buffers plus a
// snapshot of neighbor borders, and produces a MeshData (PackedVertex +
// WaterVertex vectors + index vectors). No GL calls, so we can exercise
// it here without an OpenGL context.
//
// Face convention (from FACE_DEFS in chunk_mesh.cpp):
//   0 = Front  (+Z)   1 = Back   (-Z)   2 = Left   (-X)
//   3 = Right  (+X)   4 = Top    (+Y)   5 = Bottom (-Y)
// Each visible face emits 4 vertices and 6 indices.

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include "chunk_mesh.h"
#include "cube.h"
#include "light_data.h"
#include "mesh_types.h"

namespace {

constexpr size_t BLOCK_COUNT = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

// Scratch buffers + helpers. AIR-initialized blocks, fully sky-lit packed
// light, no water flow. Each test nudges the state it cares about.
struct MeshInputs {
    std::unique_ptr<Cube[]> blocks{new Cube[BLOCK_COUNT]};
    std::unique_ptr<uint8_t[]> light{new uint8_t[BLOCK_COUNT]};
    std::unique_ptr<uint8_t[]> water{new uint8_t[BLOCK_COUNT]};
    NeighborBorders borders;
    int maxSolidY = 0;

    MeshInputs() {
        // blocks default-construct to AIR (Cube{} is AIR). Zero for safety.
        for (size_t i = 0; i < BLOCK_COUNT; i++) blocks[i].setType(AIR);
        // Full sky light everywhere, no block light — matches an open-sky
        // chunk well above terrain. Values chosen so packedLight isn't 0.
        for (size_t i = 0; i < BLOCK_COUNT; i++) light[i] = packLight(15, 0);
        std::memset(water.get(), 0, BLOCK_COUNT);
    }

    void setBlock(int x, int y, int z, block_type t) {
        blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].setType(t);
        if (t != AIR && y > maxSolidY) maxSolidY = y;
    }

    MeshData build() {
        return buildMeshFromData(blocks.get(), light.get(), water.get(),
                                 maxSolidY, /*chunkX=*/0, /*chunkZ=*/0, borders);
    }
};

// How many quads were emitted in the opaque pass? Each quad = 4 verts = 6 indices.
size_t opaqueFaceCount(const MeshData& m) { return m.opaqueIdx.size() / 6; }
size_t waterFaceCount(const MeshData& m) { return m.waterIdx.size() / 6; }

// Access the Nth PackedVertex in the opaque buffer.
const PackedVertex& opaqueVert(const MeshData& m, size_t i) {
    return *reinterpret_cast<const PackedVertex*>(&m.verts[i * sizeof(PackedVertex)]);
}
const WaterVertex& waterVert(const MeshData& m, size_t i) {
    return *reinterpret_cast<const WaterVertex*>(&m.waterVerts[i * sizeof(WaterVertex)]);
}

// Unique face normals present in the opaque output.
std::array<int, 6> countFacesByNormal(const MeshData& m) {
    std::array<int, 6> counts{};
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    // Every 4 vertices = 1 face; they share the same normalIdx.
    for (size_t i = 0; i < verts; i += 4) counts[opaqueVert(m, i).normalIdx]++;
    return counts;
}

// Pins greedy meshing on/off for one test and restores the global after.
// Tests that count faces per block must pin it off — greedy is the default.
struct GreedyGuard {
    bool prev = g_greedyMeshing;
    explicit GreedyGuard(bool on) { g_greedyMeshing = on; }
    ~GreedyGuard() { g_greedyMeshing = prev; }
    GreedyGuard(const GreedyGuard&) = delete;
    GreedyGuard& operator=(const GreedyGuard&) = delete;
};

} // namespace

TEST(ChunkMesh, EmptyChunkProducesNoGeometry) {
    MeshInputs in;
    MeshData m = in.build();
    EXPECT_TRUE(m.ready);
    EXPECT_EQ(m.verts.size(), 0u);
    EXPECT_EQ(m.waterVerts.size(), 0u);
    EXPECT_EQ(m.opaqueIdx.size(), 0u);
    EXPECT_EQ(m.waterIdx.size(), 0u);
}

TEST(ChunkMesh, SingleIsolatedBlockEmitsSixFaces) {
    MeshInputs in;
    in.setBlock(8, 8, 8, STONE);
    MeshData m = in.build();

    EXPECT_EQ(opaqueFaceCount(m), 6u);
    EXPECT_EQ(m.verts.size(), 6u * 4u * sizeof(PackedVertex));
    EXPECT_EQ(m.opaqueIdx.size(), 6u * 6u);

    // One face per normal direction (0..5).
    auto counts = countFacesByNormal(m);
    for (int f = 0; f < 6; f++) EXPECT_EQ(counts[f], 1) << "face " << f << " missing";
}

TEST(ChunkMesh, TwoAdjacentBlocksShareInternalFaces) {
    // Two stones stacked on the +X axis → the touching faces (block A's +X
    // and block B's -X) are hidden. Total visible = 2*6 - 2 = 10 (per-block
    // faces, so greedy merging is pinned off).
    GreedyGuard g(false);
    MeshInputs in;
    in.setBlock(5, 8, 8, STONE);
    in.setBlock(6, 8, 8, STONE);
    MeshData m = in.build();
    EXPECT_EQ(opaqueFaceCount(m), 10u);
}

TEST(ChunkMesh, NeighborBorderOpaqueBlockSuppressesEdgeFace) {
    // Stone at x=0 would normally emit a -X face. If the -X neighbor has
    // STONE at the touching position, that face should be hidden.
    MeshInputs in;
    in.setBlock(0, 8, 8, STONE);

    // No valid neighbor → missing-neighbor policy treats neighbor as STONE,
    // which also hides the border face. First confirm that policy:
    MeshData noNeighbor = in.build();
    auto noNeighborCounts = countFacesByNormal(noNeighbor);
    EXPECT_EQ(noNeighborCounts[2], 0) << "missing neighbor suppresses -X face";

    // With a valid neighbor holding STONE at matching cell:
    in.borders.xNeg.valid = true;
    in.borders.xNeg.types[8][8] = STONE; // types[z][y]
    MeshData withStone = in.build();
    EXPECT_EQ(countFacesByNormal(withStone)[2], 0) << "solid -X neighbor hides face";

    // Reset to AIR — now the -X face should appear.
    in.borders.xNeg.types[8][8] = AIR;
    MeshData withAir = in.build();
    EXPECT_EQ(countFacesByNormal(withAir)[2], 1) << "AIR -X neighbor exposes face";
}

TEST(ChunkMesh, InteriorTopFaceHasFullAmbientOcclusion) {
    // A lone block's top face with no neighbors around → every corner has
    // 3 empty cells (s1=s2=cr=0) → ao = 3 (full light). Writable as 3 in
    // the PackedVertex's ao byte.
    MeshInputs in;
    in.setBlock(8, 8, 8, STONE);
    MeshData m = in.build();

    // Locate the top face (normalIdx == 4) and check its 4 vertex AOs.
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    int topVerts = 0;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 4) continue;
        topVerts++;
        for (int k = 0; k < 4; k++) {
            EXPECT_EQ(opaqueVert(m, i + k).ao, 3) << "vertex " << k << " not full-lit";
        }
    }
    EXPECT_EQ(topVerts, 1);
}

TEST(ChunkMesh, CornerNeighborProducesReducedAO) {
    // Block at (8,8,8). Place a block at (9,9,8): diagonally adjacent on
    // the top+right edge of the top face. That corner's ao drops.
    MeshInputs in;
    in.setBlock(8, 8, 8, STONE);
    in.setBlock(9, 9, 8, STONE); // cr=1 for the top face's +X corner

    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);

    // Find the top (+Y) face — there's now only one such on the original block.
    // Its four corners include one that touches (9,9,8). At least one AO should be < 3.
    bool foundReducedAO = false;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 4) continue;
        // The +Y face of (8,8,8). Check if any corner is < 3.
        for (int k = 0; k < 4; k++) {
            if (opaqueVert(m, i + k).ao < 3) foundReducedAO = true;
        }
    }
    EXPECT_TRUE(foundReducedAO) << "diagonal corner block should darken at least one AO vertex";
}

TEST(ChunkMesh, WaterSourceBlockEmitsTopAndSides) {
    // A single water source block (raw=0 = source, full level).
    // Water never emits a bottom face (f==5 is skipped), so expect:
    // top (+Y) + 4 side faces = 5 water faces.
    MeshInputs in;
    in.setBlock(8, 8, 8, WATER);
    // raw=0 means level 0 = source
    MeshData m = in.build();

    EXPECT_EQ(waterFaceCount(m), 5u);
    EXPECT_EQ(opaqueFaceCount(m), 0u);
}

TEST(ChunkMesh, WaterTopCornerHeightMatchesFlowLevel) {
    // Level 0 (source, not falling) → flowing height 15/16 ≈ 0.9375.
    // Top face Y in world coords is block center (y + 0.5) minus block
    // height, then +water-height. With isolated source, corner heights
    // converge to (flowing height) (all 4 cells around corner: 1 water
    // + 3 air contributing 0 each + counted) → average ≈ (15/16) / 4.
    // We just verify ordering: higher flow level → lower surface.
    MeshInputs inHi, inLo;
    inHi.setBlock(8, 8, 8, WATER);
    inHi.water[lightIdx(8, 8, 8)] = 0; // level 0 source
    inLo.setBlock(8, 8, 8, WATER);
    inLo.water[lightIdx(8, 8, 8)] = 7; // level 7 (driest)

    MeshData hi = inHi.build();
    MeshData lo = inLo.build();

    // Find the top (+Y) water face in each and compare any vertex's py.
    auto topY = [](const MeshData& m) -> float {
        size_t verts = m.waterVerts.size() / sizeof(WaterVertex);
        for (size_t i = 0; i < verts; i += 4)
            if (waterVert(m, i).normalIdx == 4) return waterVert(m, i).py;
        return -1.0f;
    };
    EXPECT_GT(topY(hi), topY(lo))
        << "level-0 water should sit visibly higher than level-7 water";
}

TEST(ChunkMesh, FallingWaterRendersFullHeight) {
    // Falling water (raw has the falling bit) is rendered as a full block
    // so the column looks solid. Top face should be at block ceiling.
    MeshInputs inFall, inSource;
    inFall.setBlock(8, 8, 8, WATER);
    inFall.water[lightIdx(8, 8, 8)] = WATER_FALLING_FLAG; // falling
    inSource.setBlock(8, 8, 8, WATER);
    inSource.water[lightIdx(8, 8, 8)] = 0;

    MeshData fall = inFall.build();
    MeshData src = inSource.build();

    auto topY = [](const MeshData& m) -> float {
        size_t verts = m.waterVerts.size() / sizeof(WaterVertex);
        for (size_t i = 0; i < verts; i += 4)
            if (waterVert(m, i).normalIdx == 4) return waterVert(m, i).py;
        return -1.0f;
    };
    // Falling water should sit at or above a source block.
    EXPECT_GE(topY(fall), topY(src));
}

TEST(ChunkMesh, VertexWorldPositionsReflectChunkCoord) {
    // Positions are encoded as (world_coord * 2) in int16. Placing a
    // chunk at (chunkX=3, chunkZ=-2) should shift every packed vertex
    // by +3*CHUNK_SIZE*2 on X and -2*CHUNK_SIZE*2 on Z.
    MeshInputs in;
    in.setBlock(0, 8, 0, STONE);

    auto buildAt = [&](int cx, int cz) {
        return buildMeshFromData(in.blocks.get(), in.light.get(), in.water.get(),
                                 in.maxSolidY, cx, cz, in.borders);
    };
    MeshData a = buildAt(0, 0);
    MeshData b = buildAt(3, -2);

    ASSERT_GT(a.verts.size(), 0u);
    ASSERT_EQ(a.verts.size(), b.verts.size());

    // Compare vertex 0 (any corner works; they all shift by the same offset).
    int16_t dx = opaqueVert(b, 0).px - opaqueVert(a, 0).px;
    int16_t dz = opaqueVert(b, 0).pz - opaqueVert(a, 0).pz;
    EXPECT_EQ(dx, 3 * CHUNK_SIZE * 2);
    EXPECT_EQ(dz, -2 * CHUNK_SIZE * 2);
}

TEST(ChunkMesh, FaceTexLayerMatchesBlockType) {
    // A stone block's face should have texLayer == STONE (or the
    // face-specific layer for blocks with per-face textures).
    MeshInputs in;
    in.setBlock(8, 8, 8, STONE);
    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    for (size_t i = 0; i < verts; i += 4) {
        EXPECT_EQ(opaqueVert(m, i).texLayer, static_cast<uint8_t>(STONE));
    }
}

TEST(ChunkMesh, SkyLightBakedIntoVertexPackedLight) {
    // Top face of a block with sky=15 above should pack sky=15 into the
    // high nibble of packedLight → packedLight == 15 * 16 = 240.
    MeshInputs in;
    in.setBlock(8, 8, 8, STONE);
    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 4) continue;
        for (int k = 0; k < 4; k++) {
            EXPECT_EQ(opaqueVert(m, i + k).packedLight, static_cast<uint8_t>(15 * 16));
        }
    }
}

TEST(ChunkMesh, EachOpaqueFaceHas4Vertsand6Indices) {
    // Structural: verts count and index count must always be 4:6 per face.
    MeshInputs in;
    for (int x = 0; x < 4; x++)
        for (int z = 0; z < 4; z++)
            in.setBlock(x, 8, z, STONE);
    MeshData m = in.build();

    size_t verts = m.verts.size() / sizeof(PackedVertex);
    size_t idxs = m.opaqueIdx.size();
    EXPECT_EQ(verts % 4, 0u);
    EXPECT_EQ(idxs % 6, 0u);
    EXPECT_EQ(verts / 4, idxs / 6) << "one quad per 4 verts / 6 indices";

    // Indices must all reference existing vertices.
    for (unsigned idx : m.opaqueIdx) EXPECT_LT(idx, verts);
}

// A glowstone on the +X edge (x=15) is bordered by the neighbor chunk's
// AIR cell at the equivalent edge. When the flood-block-light BFS writes
// block-light=14 into that neighbor cell, its packed light is captured in
// NeighborBorders.xPos.lightBorder. The glowstone's +X face should then be
// rendered with that border light; otherwise the face goes out as dark
// (block-light=0) even though the neighbor cell is visibly lit.
TEST(ChunkMesh, GlowstoneOnPosXEdgeFaceUsesNeighborBorderLight) {
    MeshInputs in;
    constexpr int EDGE_X = CHUNK_SIZE - 1;
    constexpr int Y = 8;
    constexpr int Z = 8;
    in.setBlock(EDGE_X, Y, Z, GLOWSTONE);
    // Glowstone itself emits 15; the cell inside the glowstone block is set
    // by the caller of buildMeshFromData. Here we only populate the +X face
    // sampling inputs.
    in.light[lightIdx(EDGE_X, Y, Z)] = packLight(15, 15);

    // +X neighbor (chunk B): AIR cell, receiving block-light 14 from the
    // glowstone via floodBlockLight (one step away → 15-1=14).
    in.borders.xPos.valid = true;
    in.borders.xPos.types[Z][Y] = AIR;                    // types[z][y]
    in.borders.xPos.lightBorder[Z][Y] = packLight(15, 14); // lightBorder[z][y]

    MeshData m = in.build();

    // Find the +X face (normalIdx == 3) of the glowstone. It should exist
    // (AIR neighbor → emitted), and all four vertices should have non-zero
    // block-light because the sampler must read from nb.xPos.lightBorder.
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    int foundFaces = 0;
    int zeroBlockLightVerts = 0;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 3) continue;  // +X face
        foundFaces++;
        for (int k = 0; k < 4; k++) {
            uint8_t blk = unpackBlock(opaqueVert(m, i + k).packedLight);
            if (blk == 0) zeroBlockLightVerts++;
        }
    }
    EXPECT_EQ(foundFaces, 1) << "glowstone +X face at chunk edge not emitted";
    EXPECT_EQ(zeroBlockLightVerts, 0)
        << "glowstone +X face vertices fell back to block-light=0 instead of "
           "reading nb.xPos.lightBorder — outward face renders dark";
}

// Symmetric coverage for the other three chunk-edge directions. Keeps
// future regressions (e.g., flipping a condition) from masking only one axis.
TEST(ChunkMesh, GlowstoneOnNegXEdgeFaceUsesNeighborBorderLight) {
    MeshInputs in;
    in.setBlock(0, 8, 8, GLOWSTONE);
    in.light[lightIdx(0, 8, 8)] = packLight(15, 15);
    in.borders.xNeg.valid = true;
    in.borders.xNeg.types[8][8] = AIR;
    in.borders.xNeg.lightBorder[8][8] = packLight(15, 14);
    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    int zeroBlockLightVerts = 0, foundFaces = 0;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 2) continue;
        foundFaces++;
        for (int k = 0; k < 4; k++)
            if (unpackBlock(opaqueVert(m, i + k).packedLight) == 0) zeroBlockLightVerts++;
    }
    EXPECT_EQ(foundFaces, 1);
    EXPECT_EQ(zeroBlockLightVerts, 0);
}

TEST(ChunkMesh, GlowstoneOnPosZEdgeFaceUsesNeighborBorderLight) {
    MeshInputs in;
    in.setBlock(8, 8, CHUNK_SIZE - 1, GLOWSTONE);
    in.light[lightIdx(8, 8, CHUNK_SIZE - 1)] = packLight(15, 15);
    in.borders.zPos.valid = true;
    in.borders.zPos.types[8][8] = AIR;         // types[x][y]
    in.borders.zPos.lightBorder[8][8] = packLight(15, 14);
    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    int zeroBlockLightVerts = 0, foundFaces = 0;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 0) continue;  // +Z face
        foundFaces++;
        for (int k = 0; k < 4; k++)
            if (unpackBlock(opaqueVert(m, i + k).packedLight) == 0) zeroBlockLightVerts++;
    }
    EXPECT_EQ(foundFaces, 1);
    EXPECT_EQ(zeroBlockLightVerts, 0);
}

// --- Greedy meshing ------------------------------------------------------------
//
// Merged quads only sample AO + light at their 4 corners, so a merge is only
// correct when every covered unit face has the same, uniform AO and light.

namespace {

// Axis (0=x, 1=y, 2=z) a face's normal points along.
int normalAxis(int normalIdx) {
    return normalIdx <= 1 ? 2 : (normalIdx <= 3 ? 0 : 1);
}

struct QuadInfo {
    int normal;
    std::array<int, 3> lo, hi; // ×2-encoded position bounds
    std::array<uint8_t, 4> ao, light;
};

std::vector<QuadInfo> opaqueQuads(const MeshData& m) {
    std::vector<QuadInfo> out;
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    for (size_t i = 0; i + 3 < verts; i += 4) {
        QuadInfo q{};
        q.normal = opaqueVert(m, i).normalIdx;
        q.lo = {INT32_MAX, INT32_MAX, INT32_MAX};
        q.hi = {INT32_MIN, INT32_MIN, INT32_MIN};
        for (int k = 0; k < 4; k++) {
            const PackedVertex& pv = opaqueVert(m, i + k);
            int p[3] = {pv.px, pv.py, pv.pz};
            for (int a = 0; a < 3; a++) {
                q.lo[a] = std::min(q.lo[a], p[a]);
                q.hi[a] = std::max(q.hi[a], p[a]);
            }
            q.ao[k] = pv.ao;
            q.light[k] = pv.packedLight;
        }
        out.push_back(q);
    }
    return out;
}

// Every greedy quad must cover only unit faces whose non-greedy shading it
// reproduces exactly: 1×1 quads match vertex-for-vertex, merged quads cover
// faces that are all uniform and equal to the quad's (uniform) values.
void expectGreedyPreservesShading(MeshInputs& in) {
    MeshData plain, greedy;
    {
        GreedyGuard g(false);
        plain = in.build();
    }
    {
        GreedyGuard g(true);
        greedy = in.build();
    }
    std::map<std::array<int, 4>, QuadInfo> unit; // (normal, lo) → face
    for (const QuadInfo& q : opaqueQuads(plain)) unit[{q.normal, q.lo[0], q.lo[1], q.lo[2]}] = q;

    size_t covered = 0;
    for (const QuadInfo& q : opaqueQuads(greedy)) {
        int na = normalAxis(q.normal);
        int ta = (na + 1) % 3, tb = (na + 2) % 3;
        bool merged = (q.hi[ta] - q.lo[ta]) > 2 || (q.hi[tb] - q.lo[tb]) > 2;
        for (int a = q.lo[ta]; a < q.hi[ta]; a += 2) {
            for (int b = q.lo[tb]; b < q.hi[tb]; b += 2) {
                std::array<int, 3> lo = q.lo;
                lo[ta] = a;
                lo[tb] = b;
                auto it = unit.find({q.normal, lo[0], lo[1], lo[2]});
                ASSERT_NE(it, unit.end()) << "greedy quad covers a face the plain mesher didn't emit";
                ++covered;
                const QuadInfo& u = it->second;
                if (!merged) {
                    EXPECT_EQ(u.ao, q.ao);
                    EXPECT_EQ(u.light, q.light);
                    continue;
                }
                for (int k = 0; k < 4; k++) {
                    EXPECT_EQ(u.ao[k], q.ao[0]) << "merged over non-uniform / different AO";
                    EXPECT_EQ(u.light[k], q.light[0]) << "merged over different light";
                    EXPECT_EQ(q.ao[k], q.ao[0]);
                    EXPECT_EQ(q.light[k], q.light[0]);
                }
            }
        }
    }
    EXPECT_EQ(covered, unit.size()) << "greedy output must cover exactly the plain faces";
}

// 6×6 stone floor at y = 10 in the middle of the chunk.
void stoneFloor(MeshInputs& in) {
    for (int x = 5; x <= 10; x++)
        for (int z = 5; z <= 10; z++) in.setBlock(x, 10, z, STONE);
}

size_t topFaces(const MeshData& m) {
    return static_cast<size_t>(countFacesByNormal(m)[4]);
}

} // namespace

TEST(ChunkMesh, GreedyMergesUniformFloorIntoOneQuad) {
    MeshInputs in;
    stoneFloor(in);
    GreedyGuard g(true);
    EXPECT_EQ(topFaces(in.build()), 1u);
}

TEST(ChunkMesh, GreedyDoesNotMergeAcrossLightChange) {
    MeshInputs in;
    stoneFloor(in);
    // Block light pooling above the middle of the floor (e.g. glowstone).
    in.light[lightIdx(7, 11, 7)] = packLight(15, 14);
    GreedyGuard g(true);
    MeshData m = in.build();
    EXPECT_GT(topFaces(m), 1u);
    // The lit face survives as its own 1×1 quad with its light intact.
    int litQuads = 0;
    for (const QuadInfo& q : opaqueQuads(m)) {
        if (q.normal != 4 || unpackBlock(q.light[0]) != 14) continue;
        ++litQuads;
        EXPECT_EQ(q.hi[0] - q.lo[0], 2);
        EXPECT_EQ(q.hi[2] - q.lo[2], 2);
    }
    EXPECT_EQ(litQuads, 1);
}

TEST(ChunkMesh, GreedyDoesNotStretchAOCrease) {
    MeshInputs in;
    stoneFloor(in);
    in.setBlock(7, 11, 7, STONE); // pillar stub → AO darkening on the floor around it
    GreedyGuard g(true);
    MeshData m = in.build();
    bool sawOccludedTop = false;
    for (const QuadInfo& q : opaqueQuads(m))
        if (q.normal == 4)
            for (uint8_t a : q.ao) sawOccludedTop |= (a & 3) < 3;
    EXPECT_TRUE(sawOccludedTop) << "AO next to the stub must survive greedy merging";
}

TEST(ChunkMesh, GreedyPreservesShadingOnMixedScene) {
    MeshInputs in;
    stoneFloor(in);
    for (int x = 5; x <= 10; x++) in.setBlock(x, 11, 10, DIRT); // step along one edge
    in.setBlock(8, 11, 6, STONE);                               // pillar
    in.setBlock(8, 12, 6, STONE);
    in.light[lightIdx(6, 11, 8)] = packLight(15, 12); // block-lit patch
    in.light[lightIdx(7, 11, 8)] = packLight(15, 9);
    for (int z = 5; z <= 9; z++) in.light[lightIdx(9, 11, z)] = packLight(4, 0); // shadow strip
    expectGreedyPreservesShading(in);
}

// --- Greedy water ----------------------------------------------------------------

namespace {

// Water pool: sources at y = 10 over a stone floor (x, z in [lo, hi]); with
// `walls`, a stone rim at y = 10 so no top-face corner averages with air.
void waterPool(MeshInputs& in, int lo, int hi, bool walls) {
    for (int x = lo - 1; x <= hi + 1; x++)
        for (int z = lo - 1; z <= hi + 1; z++) {
            in.setBlock(x, 9, z, STONE);
            bool rim = x < lo || x > hi || z < lo || z > hi;
            if (!rim)
                in.setBlock(x, 10, z, WATER);
            else if (walls)
                in.setBlock(x, 10, z, STONE);
        }
}

struct WaterTop {
    std::array<float, 3> lo, hi; // ×2-encoded bounds
    std::array<float, 4> y;      // per-corner ×2 heights
};

std::vector<WaterTop> waterTops(const MeshData& m) {
    std::vector<WaterTop> out;
    size_t verts = m.waterVerts.size() / sizeof(WaterVertex);
    for (size_t i = 0; i + 3 < verts; i += 4) {
        if (waterVert(m, i).normalIdx != 4) continue;
        WaterTop t{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, {}};
        for (int k = 0; k < 4; k++) {
            const WaterVertex& v = waterVert(m, i + k);
            float p[3] = {v.px, v.py, v.pz};
            for (int a = 0; a < 3; a++) {
                t.lo[a] = std::min(t.lo[a], p[a]);
                t.hi[a] = std::max(t.hi[a], p[a]);
            }
            t.y[k] = v.py;
        }
        out.push_back(t);
    }
    return out;
}

MeshData buildWith(MeshInputs& in, bool greedy) {
    GreedyGuard g(greedy);
    return in.build();
}

} // namespace

TEST(ChunkMesh, GreedyMergesFlatWaterIntoOneQuad) {
    MeshInputs in;
    waterPool(in, 5, 10, /*walls=*/true);
    EXPECT_EQ(waterTops(buildWith(in, false)).size(), 36u);
    auto tops = waterTops(buildWith(in, true));
    ASSERT_EQ(tops.size(), 1u);
    for (float y : tops[0].y) EXPECT_FLOAT_EQ(y, tops[0].y[0]) << "merged water must stay flat";
}

TEST(ChunkMesh, GreedyKeepsSlopedWaterEdgesSeparate) {
    // No rim: edge tops average with air and slope, so they stay 1×1 while
    // the flat interior merges.
    MeshInputs in;
    waterPool(in, 4, 11, /*walls=*/false);
    size_t plain = waterTops(buildWith(in, false)).size();
    auto tops = waterTops(buildWith(in, true));
    EXPECT_GT(tops.size(), 1u);
    EXPECT_LT(tops.size(), plain);
    // Every merged (wider than one block) quad is flat; every sloped quad is 1×1.
    for (const WaterTop& t : tops) {
        bool flat = std::all_of(t.y.begin(), t.y.end(), [&](float y) { return y == t.y[0]; });
        bool unit = (t.hi[0] - t.lo[0]) == 2.0f && (t.hi[2] - t.lo[2]) == 2.0f;
        EXPECT_TRUE(flat || unit) << "sloped water quad was merged";
    }
}

TEST(ChunkMesh, GreedyNeverMergesFlowingWater) {
    MeshInputs in;
    waterPool(in, 5, 10, /*walls=*/true);
    for (int x = 5; x <= 10; x++)
        for (int z = 5; z <= 10; z++) in.water[lightIdx(x, 10, z)] = 3; // flow level 3
    EXPECT_EQ(waterTops(buildWith(in, true)).size(), waterTops(buildWith(in, false)).size());
}

TEST(ChunkMesh, GreedyWaterMatchesPlainSurfaceHeights) {
    // Mixed pool (rim on two sides, air on the others): every greedy water
    // quad must cover unit faces that the plain mesher draws flat at the
    // quad's height.
    MeshInputs in;
    waterPool(in, 4, 11, /*walls=*/false);
    for (int i = 3; i <= 12; i++) {
        in.setBlock(i, 10, 3, STONE);
        in.setBlock(3, 10, i, STONE);
    }
    std::map<std::pair<int, int>, WaterTop> unit;
    for (const WaterTop& t : waterTops(buildWith(in, false)))
        unit[{static_cast<int>(t.lo[0]), static_cast<int>(t.lo[2])}] = t;
    size_t covered = 0;
    for (const WaterTop& t : waterTops(buildWith(in, true))) {
        for (float x = t.lo[0]; x < t.hi[0]; x += 2.0f)
            for (float z = t.lo[2]; z < t.hi[2]; z += 2.0f) {
                auto it = unit.find({static_cast<int>(x), static_cast<int>(z)});
                ASSERT_NE(it, unit.end());
                covered++;
                bool merged = (t.hi[0] - t.lo[0]) > 2.0f || (t.hi[2] - t.lo[2]) > 2.0f;
                if (!merged) continue;
                for (float y : it->second.y) EXPECT_FLOAT_EQ(y, t.y[0]) << "merged over a non-flat water face";
            }
    }
    EXPECT_EQ(covered, unit.size());
}

TEST(ChunkMesh, GlowstoneOnNegZEdgeFaceUsesNeighborBorderLight) {
    MeshInputs in;
    in.setBlock(8, 8, 0, GLOWSTONE);
    in.light[lightIdx(8, 8, 0)] = packLight(15, 15);
    in.borders.zNeg.valid = true;
    in.borders.zNeg.types[8][8] = AIR;
    in.borders.zNeg.lightBorder[8][8] = packLight(15, 14);
    MeshData m = in.build();
    size_t verts = m.verts.size() / sizeof(PackedVertex);
    int zeroBlockLightVerts = 0, foundFaces = 0;
    for (size_t i = 0; i < verts; i += 4) {
        if (opaqueVert(m, i).normalIdx != 1) continue;  // -Z face
        foundFaces++;
        for (int k = 0; k < 4; k++)
            if (unpackBlock(opaqueVert(m, i + k).packedLight) == 0) zeroBlockLightVerts++;
    }
    EXPECT_EQ(foundFaces, 1);
    EXPECT_EQ(zeroBlockLightVerts, 0);
}
