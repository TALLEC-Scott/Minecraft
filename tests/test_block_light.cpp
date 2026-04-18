#include <gtest/gtest.h>

#include "ChunkManager.h"
#include "chunk.h"
#include "cube.h"
#include "light_data.h"
#include "light_propagation.h"

// Regression tests for the glowstone+destroyBlock bug: a non-emissive block
// destroyed next to an active light source needs to pick up block light
// from its neighbors. Before the fix, destroyBlock flooded sky light but
// never re-flooded block light unless the destroyed block was itself an
// emitter - so a newly-exposed cell stayed at block-light 0 even with a
// glowstone right next door.
//
// `destroyWithBlockLightRelight` mirrors the exact seeding logic in
// World::destroyBlock: scan 6 face neighbors, take max block-light, flood
// from the new AIR cell with max-1.

namespace {

void destroyWithBlockLightRelight(ChunkManager& cm, int bx, int by, int bz) {
    uint8_t maxNeighbor = 0;
    for (int i = 0; i < 6; ++i) {
        int nx = bx + DIRS_6[i][0];
        int ny = by + DIRS_6[i][1];
        int nz = bz + DIRS_6[i][2];
        int cx = worldToChunk(nx);
        int cz = worldToChunk(nz);
        Chunk* nc = cm.getChunk(cx, cz);
        if (!nc) continue;
        nc->ensureSkyLightFlat();
        int lx = worldToLocal(nx, cx);
        int lz = worldToLocal(nz, cz);
        uint8_t nl = unpackBlock(nc->skyLight.get()[lx * CHUNK_HEIGHT * CHUNK_SIZE + ny * CHUNK_SIZE + lz]);
        if (nl > maxNeighbor) maxNeighbor = nl;
    }
    if (maxNeighbor > 0) floodBlockLight(&cm, bx, by, bz, maxNeighbor - 1);
}

uint8_t blockLightAt(ChunkManager& cm, int bx, int by, int bz) {
    int cx = worldToChunk(bx);
    int cz = worldToChunk(bz);
    Chunk* c = cm.getChunk(cx, cz);
    if (!c || !c->skyLight) return 0;
    int lx = worldToLocal(bx, cx);
    int lz = worldToLocal(bz, cz);
    return unpackBlock(c->skyLight.get()[lx * CHUNK_HEIGHT * CHUNK_SIZE + by * CHUNK_SIZE + lz]);
}

// Force a cell's packed block light (bypasses the flood). Used to set up a
// scenario where one neighbor is already lit so we can test the relight
// helper in isolation.
void setBlockLight(ChunkManager& cm, int bx, int by, int bz, uint8_t val) {
    int cx = worldToChunk(bx);
    int cz = worldToChunk(bz);
    Chunk* c = cm.getChunk(cx, cz);
    ASSERT_NE(c, nullptr);
    c->ensureSkyLightFlat();
    int lx = worldToLocal(bx, cx);
    int lz = worldToLocal(bz, cz);
    size_t i = lx * CHUNK_HEIGHT * CHUNK_SIZE + by * CHUNK_SIZE + lz;
    c->skyLight.get()[i] = (c->skyLight.get()[i] & 0xF0) | (val & 0xF);
}

} // namespace

class BlockLightDestroyRelightTest : public ::testing::Test {
  protected:
    ChunkManager cm;
    void SetUp() override { cm.getOrCreate(0, 0); }
};

TEST_F(BlockLightDestroyRelightTest, FloodFromSourcePropagatesLinearly) {
    Chunk& c = cm.getOrCreate(0, 0);
    c.setBlockType(5, 5, 5, GLOWSTONE);
    floodBlockLight(&cm, 5, 5, 5, 15);
    EXPECT_EQ(blockLightAt(cm, 5, 5, 5), 15);
    EXPECT_EQ(blockLightAt(cm, 6, 5, 5), 14);
    EXPECT_EQ(blockLightAt(cm, 7, 5, 5), 13);
    EXPECT_EQ(blockLightAt(cm, 5, 5, 20), 0); // past reach
}

TEST_F(BlockLightDestroyRelightTest, DestroyedCellInheritsMaxNeighborMinusOne) {
    // Before the fix: destroyBlock on a non-emitter never flooded block
    // light, so a cell adjacent to a lit neighbor stayed dark. Simulate
    // the bug scenario directly: seed a neighbor's block light to 15,
    // leave our cell at 0, then run the relight helper.
    cm.getOrCreate(0, 0);
    setBlockLight(cm, 5, 5, 5, 15); // neighbor is glowstone-level lit

    // Our target cell starts dark (as if just destroyed).
    EXPECT_EQ(blockLightAt(cm, 6, 5, 5), 0);

    destroyWithBlockLightRelight(cm, 6, 5, 5);

    EXPECT_EQ(blockLightAt(cm, 6, 5, 5), 14) << "newly-exposed cell should inherit max neighbor - 1";
    EXPECT_EQ(blockLightAt(cm, 7, 5, 5), 13) << "light should cascade one step further";
    EXPECT_EQ(blockLightAt(cm, 8, 5, 5), 12);
}

TEST_F(BlockLightDestroyRelightTest, DestroyAwayFromAnyLightStaysDark) {
    // No lit neighbors → relight helper does nothing.
    cm.getOrCreate(0, 0);
    destroyWithBlockLightRelight(cm, 10, 5, 5);
    EXPECT_EQ(blockLightAt(cm, 10, 5, 5), 0);
    EXPECT_EQ(blockLightAt(cm, 11, 5, 5), 0);
}

TEST_F(BlockLightDestroyRelightTest, DimNeighborProducesDimmerPropagation) {
    // If the max neighbor is only at light level 5, the flood starts at 4
    // and cascades down to zero over 4 more cells.
    cm.getOrCreate(0, 0);
    setBlockLight(cm, 5, 5, 5, 5);

    destroyWithBlockLightRelight(cm, 6, 5, 5);

    EXPECT_EQ(blockLightAt(cm, 6, 5, 5), 4);
    EXPECT_EQ(blockLightAt(cm, 7, 5, 5), 3);
    EXPECT_EQ(blockLightAt(cm, 8, 5, 5), 2);
    EXPECT_EQ(blockLightAt(cm, 9, 5, 5), 1);
    EXPECT_EQ(blockLightAt(cm, 10, 5, 5), 0); // runs out
}
