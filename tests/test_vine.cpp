// Vines (vine.h) and jungle leaves: block properties, the state byte (which
// sides a vine covers), and how the offline mesher draws a vine: one flat
// quad per covered side, on that side's face, in the cross-plant index range,
// without cube faces and without hiding its neighbours' faces.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <set>

#include "block_layers.h"
#include "chunk_mesh.h"
#include "cube.h"
#include "light_data.h"
#include "mesh_types.h"
#include "mining.h"
#include "vine.h"

namespace {

constexpr size_t CELLS = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

size_t cellIndex(int x, int y, int z) {
    return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
}

struct Scene {
    std::unique_ptr<Cube[]> blocks{new Cube[CELLS]};
    std::unique_ptr<uint8_t[]> light{new uint8_t[CELLS]};
    std::unique_ptr<uint8_t[]> state{new uint8_t[CELLS]()};
    NeighborBorders borders;
    int maxSolidY = 0;

    Scene() {
        for (size_t i = 0; i < CELLS; i++) light[i] = packLight(15, 0);
    }
    void set(int x, int y, int z, block_type t, uint8_t s = 0) {
        blocks[cellIndex(x, y, z)].setType(t);
        state[cellIndex(x, y, z)] = s;
        if (y > maxSolidY) maxSolidY = y;
    }
    MeshData build() { return buildMeshFromData(blocks.get(), light.get(), state.get(), maxSolidY, 0, 0, borders); }
};

const PackedVertex& vert(const MeshData& m, unsigned int i) {
    return *reinterpret_cast<const PackedVertex*>(&m.verts[static_cast<size_t>(i) * sizeof(PackedVertex)]);
}

struct GreedyOff {
    bool prev = g_greedyMeshing;
    GreedyOff() { g_greedyMeshing = false; }
    ~GreedyOff() { g_greedyMeshing = prev; }
    GreedyOff(const GreedyOff&) = delete;
    GreedyOff& operator=(const GreedyOff&) = delete;
};

} // namespace

TEST(VineTest, BlockProperties) {
    EXPECT_TRUE(hasFlag(VINE, BF_CROSS));
    EXPECT_FALSE(hasFlag(VINE, BF_SOLID)) << "no collision";
    EXPECT_FALSE(hasFlag(VINE, BF_OPAQUE)) << "doesn't block light";
    EXPECT_FALSE(hasFlag(VINE, BF_TRANSLUCENT)) << "doesn't dim sky light";
    EXPECT_FALSE(isAOOccluder(VINE));
    EXPECT_EQ(getBlockLightEmission(VINE), 0);
    EXPECT_EQ(breakTicks(VINE, ITEM_EMPTY), 0) << "breaks instantly";
    EXPECT_EQ(blockDrop(VINE, ITEM_EMPTY, 0.5f).count, 0) << "needs shears, which don't exist";
    EXPECT_EQ(block_layers::layerForFace(VINE, 0), block_layers::VINE_LAYER);
    EXPECT_EQ(block_layers::layerForType(VINE), block_layers::VINE_LAYER);
}

TEST(VineTest, JungleLeavesAreLeaves) {
    EXPECT_TRUE(isLeaves(JUNGLE_LEAVES));
    EXPECT_TRUE(hasFlag(JUNGLE_LEAVES, BF_SOLID) && hasFlag(JUNGLE_LEAVES, BF_TRANSLUCENT));
    EXPECT_FALSE(hasFlag(JUNGLE_LEAVES, BF_OPAQUE));
    EXPECT_EQ(getStepSound(JUNGLE_LEAVES), STEP_GRASS);
    EXPECT_EQ(blockDrop(JUNGLE_LEAVES, ITEM_EMPTY, 0.001f).count, 0);
    for (int f = 0; f < 6; f++)
        EXPECT_EQ(block_layers::layerForFace(JUNGLE_LEAVES, f), block_layers::JUNGLE_LEAVES_LAYER);
    EXPECT_TRUE(vine::canSupport(JUNGLE_LEAVES));
    EXPECT_TRUE(vine::canSupport(JUNGLE_LOG));
    EXPECT_FALSE(vine::canSupport(AIR));
    EXPECT_FALSE(vine::canSupport(VINE));
    EXPECT_FALSE(vine::canSupport(WATER));
}

TEST(VineTest, StateFromTheClickedFace) {
    EXPECT_EQ(vine::stateForFace(0, 1, 0), vine::INVALID) << "not on top of a block";
    EXPECT_EQ(vine::stateForFace(0, -1, 0), vine::INVALID) << "not under one";
    // Clicking the -X side of a block puts the vine at -X, covering its +X side.
    EXPECT_EQ(vine::stateForFace(-1, 0, 0), vine::POS_X);
    EXPECT_EQ(vine::stateForFace(1, 0, 0), vine::NEG_X);
    EXPECT_EQ(vine::stateForFace(0, 0, -1), vine::POS_Z);
    EXPECT_EQ(vine::stateForFace(0, 0, 1), vine::NEG_Z);
    for (uint8_t s = 1; s <= vine::ALL_SIDES; s++) EXPECT_TRUE(vine::validState(s));
    EXPECT_FALSE(vine::validState(0));
    EXPECT_FALSE(vine::validState(0x10));
    EXPECT_FALSE(vine::validState(0xFF)) << "World::placeBlock's 'work it out' marker";
}

TEST(VineTest, QuadLiesOnTheCoveredFace) {
    for (int b = 0; b < 4; b++) {
        float q[4][3];
        vine::quadCorners(3.0f, 10.0f, -7.0f, b, q);
        for (auto& c : q) {
            if (vine::SIDE[b][0] != 0) {
                EXPECT_FLOAT_EQ(c[0], 3.0f + 0.5f * static_cast<float>(vine::SIDE[b][0]));
            } else {
                EXPECT_FLOAT_EQ(c[2], -7.0f + 0.5f * static_cast<float>(vine::SIDE[b][2]));
            }
        }
        EXPECT_FLOAT_EQ(q[0][1], 9.5f);
        EXPECT_FLOAT_EQ(q[1][1], 10.5f);
    }
}

TEST(VineMeshTest, OneQuadOnTheCoveredSideAndNoCubeFaces) {
    GreedyOff g;
    Scene s;
    s.set(8, 40, 8, VINE, vine::NEG_Z);
    MeshData m = s.build();
    EXPECT_EQ(m.opaqueIdx.size(), 0u) << "a vine has no cube faces";
    EXPECT_EQ(m.waterIdx.size(), 0u);
    ASSERT_EQ(m.crossIdx.size(), 6u) << "one quad";
    ASSERT_EQ(m.verts.size(), 4 * sizeof(PackedVertex));
    for (unsigned int i = 0; i < 4; i++) {
        const PackedVertex& v = vert(m, i);
        EXPECT_EQ(v.pz, 2 * 8 - 1) << "on the -Z face (positions are ×2)";
        EXPECT_EQ(v.normalIdx, 0) << "facing +Z, away from what it covers";
        EXPECT_EQ(v.texLayer, block_layers::VINE_LAYER);
        EXPECT_EQ(v.packedLight, packLight(15, 0));
    }
}

TEST(VineMeshTest, EverySideBitGetsItsOwnQuad) {
    Scene s;
    s.set(4, 30, 4, VINE, vine::POS_X | vine::NEG_X | vine::POS_Z);
    MeshData m = s.build();
    ASSERT_EQ(m.crossIdx.size(), 18u);
    std::set<int> normals;
    for (unsigned int i = 0; i < 12; i += 4) normals.insert(vert(m, i).normalIdx);
    EXPECT_EQ(normals, (std::set<int>{vine::NORMAL_IDX[0], vine::NORMAL_IDX[1], vine::NORMAL_IDX[2]}));
}

TEST(VineMeshTest, NeighbourFacesStayDrawn) {
    // A leaf with a vine on its +X face, a stone above the vine: both keep
    // the faces that touch the vine.
    GreedyOff g;
    Scene s;
    s.set(8, 40, 8, JUNGLE_LEAVES);
    s.set(9, 40, 8, VINE, vine::NEG_X);
    MeshData leafAlone = [&] {
        Scene t;
        t.set(8, 40, 8, JUNGLE_LEAVES);
        return t.build();
    }();
    MeshData withVine = s.build();
    EXPECT_EQ(withVine.opaqueIdx.size(), leafAlone.opaqueIdx.size()) << "the leaf's +X face is still drawn";
    EXPECT_EQ(withVine.opaqueIdx.size(), 36u);
    ASSERT_EQ(withVine.crossIdx.size(), 6u);
    EXPECT_EQ(vert(withVine, withVine.crossIdx[0]).px, 2 * 9 - 1) << "on the leaf's +X face, inset by the shader";
}

TEST(VineMeshTest, MissingStateStillDrawsSomething) {
    // A vine with no state (e.g. set by a path that forgot it) shows on all
    // four sides rather than vanishing while still being breakable.
    Scene s;
    s.set(2, 20, 2, VINE, 0);
    EXPECT_EQ(s.build().crossIdx.size(), 24u);
}
