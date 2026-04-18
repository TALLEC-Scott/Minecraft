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
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
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
    // and block B's -X) are hidden. Total visible = 2*6 - 2 = 10.
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
