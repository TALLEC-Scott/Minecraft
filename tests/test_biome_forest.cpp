// Forest biome (src/biomes/forest.cpp): birch groves, fancy oaks, fallen
// logs and the shaded floor, checked on generated chunks. GL-free.

#include <gtest/gtest.h>

#include <memory>

#include "TerrainGenerator.h"
#include "biomes/forest.h"
#include "block_layers.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int cx, int cz) { fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes); }
    block_type at(int x, int y, int z) const {
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

class ForestTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
};

} // namespace

TEST(ForestGrove, GrovesAreCoherentPatches) {
    // Groves cover a minority of the land, and a grove column's neighbours
    // are almost always in the grove too (patches, not speckle).
    long in = 0, total = 0, sameAsRight = 0;
    for (int x = -400; x < 400; x++)
        for (int z = -400; z < 400; z += 7) {
            const bool g = forest::inBirchGrove(42, x, z);
            total++;
            if (!g) continue;
            in++;
            if (forest::inBirchGrove(42, x + 1, z)) sameAsRight++;
        }
    const double share = static_cast<double>(in) / static_cast<double>(total);
    EXPECT_GT(share, 0.08);
    EXPECT_LT(share, 0.45);
    EXPECT_GT(static_cast<double>(sameAsRight) / static_cast<double>(in), 0.85);
    EXPECT_EQ(forest::inBirchGrove(42, 10, 10), forest::inBirchGrove(42, 10, 10));
}

TEST_F(ForestTest, BirchGrowsInGrovesOakElsewhere) {
    long birchIn = 0, oakIn = 0, birchOut = 0, oakOut = 0;
    for (int cx = -40; cx < 40; cx += 2)
        for (int cz = -40; cz < 40; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (c.biomes[x][z] != BIOME_FOREST) continue;
                    const int h = c.heights[x][z];
                    if (h + 2 >= CHUNK_HEIGHT) continue;
                    const block_type t = c.at(x, h + 1, z);
                    if (t != WOOD && t != BIRCH_LOG) continue;
                    const bool grove = forest::inBirchGrove(42, cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z);
                    const bool birch = t == BIRCH_LOG;
                    (grove ? (birch ? birchIn : oakIn) : (birch ? birchOut : oakOut))++;
                }
        }
    ASSERT_GT(birchIn + oakIn, 20) << "no grove trees in the sample";
    EXPECT_GT(birchIn, 4 * oakIn) << "groves are mostly birch";
    EXPECT_GT(oakOut, 10 * birchOut) << "outside groves birch is rare";
}

TEST_F(ForestTest, FancyOaksAndFallenLogsAppear) {
    // A fancy oak shows as a trunk of 7+ logs; fallen logs lie on the ground
    // in runs of 3..5 along one axis, never floating.
    int fancy = 0, fallen = 0;
    for (int cx = -40; cx < 40; cx += 2)
        for (int cz = -40; cz < 40; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    for (int y = 1; y < CHUNK_HEIGHT; y++) {
                        const block_type t = c.at(x, y, z);
                        if (t != OAK_LOG_X && t != OAK_LOG_Z) continue;
                        // (A log rooted at the forest's edge may reach a
                        // few blocks into the neighbouring biome.)
                        EXPECT_TRUE(c.biomes[x][z] == BIOME_FOREST || c.biomes[x][z] == BIOME_PLAINS ||
                                    c.biomes[x][z] == BIOME_SEASONAL_FOREST || c.biomes[x][z] == BIOME_TAIGA ||
                                    c.biomes[x][z] == BIOME_SWAMPLAND || c.biomes[x][z] == BIOME_JUNGLE);
                        EXPECT_EQ(y, h + 1) << "fallen log off the ground";
                        // Run start: count the run along its axis.
                        const bool alongX = t == OAK_LOG_X;
                        const int px = alongX ? x - 1 : x, pz = alongX ? z : z - 1;
                        if (px >= 0 && pz >= 0 && c.at(px, y, pz) == t) continue;
                        int len = 0;
                        while ((alongX ? x + len : z + len) < CHUNK_SIZE &&
                               c.at(alongX ? x + len : x, y, alongX ? z : z + len) == t)
                            len++;
                        EXPECT_GE(len, 3);
                        EXPECT_LE(len, 10); // two logs may touch end to end
                        fallen++;
                    }
                    if (c.biomes[x][z] != BIOME_FOREST || h + 12 >= CHUNK_HEIGHT || c.at(x, h + 1, z) != WOOD)
                        continue;
                    int top = h + 1;
                    while (c.at(x, top + 1, z) == WOOD) top++;
                    if (top - h >= 8) fancy++;
                }
        }
    EXPECT_GT(fancy, 3);
    EXPECT_GT(fallen, 3);
}

TEST_F(ForestTest, FancyOakCanopiesCrossChunkBorders) {
    // A fancy oak 3 blocks from the +X edge: its top cluster (radius 3 at
    // the trunk top) must continue in the next chunk's first column.
    int checked = 0;
    for (int cx = -40; cx < 40 && checked < 3; cx++)
        for (int cz = -40; cz < 40 && checked < 3; cz += 3) {
            GenChunk a(terrain, cx, cz);
            const int x = CHUNK_SIZE - 3;
            for (int z = 0; z < CHUNK_SIZE && checked < 3; z++) {
                if (a.biomes[x][z] != BIOME_FOREST) continue;
                const int h = a.heights[x][z];
                if (h + 14 >= CHUNK_HEIGHT || a.at(x, h + 1, z) != WOOD) continue;
                int top = h + 1;
                while (a.at(x, top + 1, z) == WOOD) top++;
                if (top - h < 8) continue;
                GenChunk b(terrain, cx + 1, cz);
                const block_type t = b.at(0, top, z);
                EXPECT_TRUE(isLeaves(t) || isLog(t)) << "fancy canopy cut at the border, trunk at world ("
                                                     << cx * CHUNK_SIZE + x << ", " << cz * CHUNK_SIZE + z << ")";
                checked++;
            }
        }
    EXPECT_GT(checked, 0) << "no fancy oak next to a border in the sample";
}

TEST(ForestBlocks, SidewaysLogsShowRingsAtTheEnds) {
    using block_layers::layerForFace;
    // Faces: 0/1 = ±Z, 2/3 = ±X, 4/5 = ±Y.
    EXPECT_EQ(layerForFace(OAK_LOG_X, 2), block_layers::WOOD_TOP_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_X, 3), block_layers::WOOD_TOP_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_X, 4), block_layers::WOOD_SIDEWAYS_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_Z, 0), block_layers::WOOD_TOP_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_Z, 1), block_layers::WOOD_TOP_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_Z, 2), block_layers::WOOD_SIDEWAYS_LAYER);
    EXPECT_EQ(layerForFace(OAK_LOG_Z, 4), static_cast<int>(WOOD));
    EXPECT_TRUE(isLog(OAK_LOG_X) && isLog(OAK_LOG_Z));
    EXPECT_EQ(blockDrop(OAK_LOG_X, ITEM_EMPTY, 0.5f).id, blockItem(WOOD));
    EXPECT_EQ(blockDrop(OAK_LOG_Z, ITEM_EMPTY, 0.5f).id, blockItem(WOOD));
}
