// Property-based coverage for the "light at a chunk edge" class of bugs.
//
// Every emissive block placed next to a chunk boundary must propagate its
// light both intra-chunk (computeBlockLightData / floodBlockLight) and into
// the rendered mesh (buildMeshFromData reading neighbor-border snapshots).
// These tests matrix-check every emissive block × every edge direction so a
// regression in any single sampler site shows up immediately.

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "chunk_mesh.h"
#include "cube.h"
#include "light_data.h"
#include "light_propagation.h"
#include "light_sampler.h"
#include "mesh_types.h"

namespace {

constexpr size_t BLOCK_COUNT = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

// Every block type the game currently treats as emissive. Property tests
// iterate this list so any new emissive type auto-inherits the suite.
const std::vector<block_type> kEmissiveBlocks = {GLOWSTONE};

// Four cardinal chunk-edge configurations. Each names the border the
// emissive block sits against, the normalIdx the outward face uses, and
// the local (x, z) position of the emissive block inside its own chunk.
struct EdgeCase {
    const char* name;
    int borderIdx;          // 0=xNeg 1=xPos 2=zNeg 3=zPos (matches NeighborBorders fields)
    int outwardNormal;      // FACE_DEFS idx: 2=-X 3=+X 1=-Z 0=+Z
    int localX, localZ;
};
const std::vector<EdgeCase> kEdges = {
    {"-X edge", 0, 2, 0, 8},
    {"+X edge", 1, 3, CHUNK_SIZE - 1, 8},
    {"-Z edge", 2, 1, 8, 0},
    {"+Z edge", 3, 0, 8, CHUNK_SIZE - 1},
};

void setBorder(NeighborBorders& nb, int borderIdx, int tangent, int y, block_type t, uint8_t light) {
    NeighborBorder* target = nullptr;
    switch (borderIdx) {
        case 0: target = &nb.xNeg; break;
        case 1: target = &nb.xPos; break;
        case 2: target = &nb.zNeg; break;
        case 3: target = &nb.zPos; break;
    }
    target->valid = true;
    target->types[tangent][y] = t;
    target->lightBorder[tangent][y] = light;
}

struct MeshInputs {
    std::unique_ptr<Cube[]> blocks{new Cube[BLOCK_COUNT]};
    std::unique_ptr<uint8_t[]> light{new uint8_t[BLOCK_COUNT]};
    std::unique_ptr<uint8_t[]> water{new uint8_t[BLOCK_COUNT]};
    NeighborBorders borders;
    int maxSolidY = 8;

    MeshInputs() {
        for (size_t i = 0; i < BLOCK_COUNT; i++) blocks[i].setType(AIR);
        for (size_t i = 0; i < BLOCK_COUNT; i++) light[i] = packLight(15, 0);
        std::memset(water.get(), 0, BLOCK_COUNT);
    }
};

const PackedVertex& vertAt(const MeshData& m, size_t i) {
    return *reinterpret_cast<const PackedVertex*>(&m.verts[i * sizeof(PackedVertex)]);
}

}  // namespace

// --- sampleLightBorder direct tests ---

TEST(LightSampler, InBoundsReadsFromMainArray) {
    auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
    for (size_t i = 0; i < BLOCK_COUNT; i++) light[i] = packLight(10, 5);
    NeighborBorders nb;
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 5, 10, 7), packLight(10, 5));
}

TEST(LightSampler, YOutOfBoundsReturnsOpenSky) {
    auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
    NeighborBorders nb;
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 5, -1, 7), packLight(15, 0));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 5, CHUNK_HEIGHT, 7), packLight(15, 0));
}

TEST(LightSampler, InvalidBorderReturnsOpenSky) {
    auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
    NeighborBorders nb; // all borders invalid
    EXPECT_EQ(sampleLightBorder(light.get(), nb, -1, 8, 8), packLight(15, 0));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, CHUNK_SIZE, 8, 8), packLight(15, 0));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 8, 8, -1), packLight(15, 0));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 8, 8, CHUNK_SIZE), packLight(15, 0));
}

TEST(LightSampler, ValidBorderReturnsBorderValue) {
    auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
    NeighborBorders nb;
    // Distinct values per direction so an off-by-one routing bug shows up.
    nb.xNeg.valid = true;
    nb.xNeg.lightBorder[3][9] = packLight(5, 11); // index [z][y]
    nb.xPos.valid = true;
    nb.xPos.lightBorder[3][9] = packLight(6, 12);
    nb.zNeg.valid = true;
    nb.zNeg.lightBorder[3][9] = packLight(7, 13); // index [x][y]
    nb.zPos.valid = true;
    nb.zPos.lightBorder[3][9] = packLight(8, 14);

    EXPECT_EQ(sampleLightBorder(light.get(), nb, -1, 9, 3), packLight(5, 11));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, CHUNK_SIZE, 9, 3), packLight(6, 12));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 3, 9, -1), packLight(7, 13));
    EXPECT_EQ(sampleLightBorder(light.get(), nb, 3, 9, CHUNK_SIZE), packLight(8, 14));
}

// --- Property: every emissive × every edge → outward face lights up ---

TEST(LightingBoundary, EmissiveOnEveryEdgeLightsOutwardFace) {
    for (block_type emissive : kEmissiveBlocks) {
        uint8_t emit = getBlockLightEmission(emissive);
        ASSERT_GT(emit, 0u) << "emissive list contains non-emissive type " << (int)emissive;

        for (const auto& edge : kEdges) {
            MeshInputs in;
            // Place the emissive at the edge cell inside chunk A.
            size_t idx = static_cast<size_t>(edge.localX) * CHUNK_HEIGHT * CHUNK_SIZE +
                         8ull * CHUNK_SIZE + edge.localZ;
            in.blocks[idx].setType(emissive);
            in.light[idx] = packLight(15, emit);
            // Simulate the post-flood state: neighbor border cell at the same
            // y has emit-1 block-light. Block-light field is what matters.
            int tangent = (edge.borderIdx < 2) ? edge.localZ : edge.localX;
            setBorder(in.borders, edge.borderIdx, tangent, 8, AIR, packLight(15, emit - 1));

            MeshData m = buildMeshFromData(in.blocks.get(), in.light.get(), in.water.get(),
                                           in.maxSolidY, 0, 0, in.borders);
            size_t verts = m.verts.size() / sizeof(PackedVertex);
            int outwardFaces = 0;
            int zeroBlockLightVerts = 0;
            for (size_t i = 0; i < verts; i += 4) {
                if (vertAt(m, i).normalIdx != edge.outwardNormal) continue;
                outwardFaces++;
                for (int k = 0; k < 4; k++)
                    if (unpackBlock(vertAt(m, i + k).packedLight) == 0) zeroBlockLightVerts++;
            }
            EXPECT_EQ(outwardFaces, 1)
                << "emissive " << (int)emissive << " on " << edge.name << " did not emit outward face";
            EXPECT_EQ(zeroBlockLightVerts, 0)
                << "emissive " << (int)emissive << " on " << edge.name
                << " had vertices with block-light=0 — the OOB sampler didn't route to the border";
        }
    }
}

// --- Property: computeBlockLightData spreads light inside the chunk ---

TEST(LightingBoundary, ComputeBlockLightFillsAdjacent) {
    for (block_type emissive : kEmissiveBlocks) {
        uint8_t emit = getBlockLightEmission(emissive);

        auto blocks = std::make_unique<Cube[]>(BLOCK_COUNT);
        auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
        for (size_t i = 0; i < BLOCK_COUNT; i++) {
            blocks[i].setType(AIR);
            light[i] = packLight(15, 0);
        }
        int cx = 8, cy = 8, cz = 8;
        blocks[lightIdx(cx, cy, cz)].setType(emissive);

        computeBlockLightData(blocks.get(), light.get(), /*maxSolidY=*/cy + 1);

        EXPECT_EQ(unpackBlock(light[lightIdx(cx, cy, cz)]), emit)
            << "source cell didn't receive its own emission for block " << (int)emissive;
        // Six cardinal neighbors must receive emit-1.
        int dx[6] = {1, -1, 0, 0, 0, 0};
        int dy[6] = {0, 0, 1, -1, 0, 0};
        int dz[6] = {0, 0, 0, 0, 1, -1};
        for (int d = 0; d < 6; d++) {
            uint8_t got = unpackBlock(light[lightIdx(cx + dx[d], cy + dy[d], cz + dz[d])]);
            EXPECT_EQ(got, emit - 1)
                << "neighbor " << d << " of " << (int)emissive << " got " << (int)got << " expected " << (emit - 1);
        }
    }
}

// --- Property: emissive at each chunk corner spreads in-bounds but does not
// set neighbor-border cells (those are the neighbor chunk's responsibility). ---

TEST(LightingBoundary, EmissiveAtEveryCornerPopulatesInteriorNeighborCells) {
    // Grid coords inside the chunk. Corners pick up extreme x/z, center y.
    const std::array<std::pair<int, int>, 4> corners = {{{0, 0}, {0, CHUNK_SIZE - 1},
                                                         {CHUNK_SIZE - 1, 0}, {CHUNK_SIZE - 1, CHUNK_SIZE - 1}}};
    for (block_type emissive : kEmissiveBlocks) {
        uint8_t emit = getBlockLightEmission(emissive);
        for (auto [x, z] : corners) {
            auto blocks = std::make_unique<Cube[]>(BLOCK_COUNT);
            auto light = std::make_unique<uint8_t[]>(BLOCK_COUNT);
            for (size_t i = 0; i < BLOCK_COUNT; i++) {
                blocks[i].setType(AIR);
                light[i] = packLight(15, 0);
            }
            blocks[lightIdx(x, 8, z)].setType(emissive);
            computeBlockLightData(blocks.get(), light.get(), /*maxSolidY=*/9);

            EXPECT_EQ(unpackBlock(light[lightIdx(x, 8, z)]), emit)
                << "corner (" << x << "," << z << ") source not lit";
            // Inward neighbors (one step toward the chunk center) should be lit.
            int inX = (x == 0) ? 1 : (CHUNK_SIZE - 2);
            int inZ = (z == 0) ? 1 : (CHUNK_SIZE - 2);
            EXPECT_EQ(unpackBlock(light[lightIdx(inX, 8, z)]), emit - 1);
            EXPECT_EQ(unpackBlock(light[lightIdx(x, 8, inZ)]), emit - 1);
        }
    }
}
