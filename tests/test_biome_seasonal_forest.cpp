// Seasonal forest (src/biomes/seasonal_forest.cpp): oak and birch woods with
// autumn-turned crowns, flowery clearings and low leaf bushes.
#include <gtest/gtest.h>

#include <map>
#include <memory>

#include "TerrainGenerator.h"
#include "biomes/seasonal_forest.h"
#include "block_layers.h"
#include "item.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr uint64_t SEED = 42;

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

bool isAutumnLeaves(block_type t) {
    return t == RED_AUTUMN_LEAVES || t == ORANGE_AUTUMN_LEAVES;
}

// Chunks around (1168, -1208), deep in a seasonal forest for seed 42.
constexpr int CX0 = 70, CX1 = 76, CZ0 = -79, CZ1 = -73;

} // namespace

TEST(SeasonalForestBlocks, AutumnLeavesAndTulipAreWiredUp) {
    for (block_type t : {RED_AUTUMN_LEAVES, ORANGE_AUTUMN_LEAVES}) {
        EXPECT_TRUE(isLeaves(t));
        EXPECT_TRUE(hasFlag(t, BF_TRANSLUCENT));
        EXPECT_EQ(blockDrop(t, ITEM_EMPTY, 0.5f).count, 0);
        EXPECT_GT(block_layers::layerForType(t), block_layers::FERN_LAYER);
    }
    EXPECT_TRUE(hasFlag(ORANGE_TULIP, BF_CROSS));
    EXPECT_FALSE(hasFlag(ORANGE_TULIP, BF_SOLID));
    EXPECT_EQ(blockDrop(ORANGE_TULIP, ITEM_EMPTY, 0.5f).id, blockItem(ORANGE_TULIP));
    EXPECT_STREQ(itemName(blockItem(ORANGE_TULIP)), "Orange Tulip");
    EXPECT_EQ(block_layers::layerForType(ORANGE_TULIP), block_layers::ORANGE_TULIP_LAYER);
}

TEST(SeasonalForestTest, PatchNoiseIsSmooth) {
    for (int z = 0; z < 150; z++)
        for (int x = 0; x < 150; x++) {
            const double c = seasonal_forest::clearing(SEED, x, z);
            ASSERT_GE(c, 0.0);
            ASSERT_LE(c, 1.0);
            EXPECT_LT(std::abs(c - seasonal_forest::clearing(SEED, x + 1, z)), 0.2);
            EXPECT_LT(std::abs(seasonal_forest::autumn(SEED, x, z) - seasonal_forest::autumn(SEED, x, z + 1)), 0.2);
        }
}

TEST(SeasonalForestTest, ClearingsAreFlowerMeadows) {
    long meadow = 0, meadowFlowers = 0, wood = 0, woodFlowers = 0;
    std::map<block_type, int> kinds;
    for (int z = 0; z < 300; z++)
        for (int x = 0; x < 300; x++) {
            const auto roll = static_cast<uint32_t>((x * 7919 + z * 104729) % 10000);
            const block_type b = seasonal_forest::groundCover(biomegen::GroundSite{GRASS, roll, x, z, SEED});
            const bool flower = b == DANDELION || b == POPPY || b == ORANGE_TULIP;
            if (flower) kinds[b]++;
            if (seasonal_forest::clearing(SEED, x, z) < seasonal_forest::CLEARING) {
                meadow++;
                meadowFlowers += flower;
            } else {
                wood++;
                woodFlowers += flower;
            }
        }
    ASSERT_GT(meadow, 1000);
    ASSERT_GT(wood, 1000);
    EXPECT_GT(static_cast<double>(meadowFlowers) / meadow, 0.08);
    EXPECT_GT(static_cast<double>(meadowFlowers) / meadow, 2.0 * woodFlowers / wood);
    EXPECT_EQ(kinds.size(), 3u);
    EXPECT_EQ(seasonal_forest::groundCover(biomegen::GroundSite{SAND, 0, 1, 1, SEED}), AIR);
}

TEST(SeasonalForestTest, MixedWoodsWithTurnedCrownsAndOnlyHere) {
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    long cols = 0, tulips = 0, flowers = 0;
    long greenLeaves = 0, autumnLeaves = 0, birchLeaves = 0;
    for (int cx = CX0; cx < CX1; cx++)
        for (int cz = CZ0; cz < CZ1; cz++) {
            GenChunk c(tg, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const bool mine = c.biomes[x][z] == BIOME_SEASONAL_FOREST;
                    if (mine) cols++;
                    for (int y = 1; y < CHUNK_HEIGHT; y++) {
                        const block_type t = c.at(x, y, z);
                        if (t == LEAVES) greenLeaves++;
                        if (t == BIRCH_LEAVES) birchLeaves++;
                        if (isAutumnLeaves(t)) autumnLeaves++;
                    }
                    const int h = c.heights[x][z];
                    const block_type above = c.at(x, h + 1, z);
                    if (above == ORANGE_TULIP) {
                        EXPECT_TRUE(mine) << "tulip outside the seasonal forest";
                        tulips++;
                    }
                    if (mine && (above == DANDELION || above == POPPY || above == ORANGE_TULIP)) flowers++;
                }
        }
    ASSERT_GT(cols, 5000);
    EXPECT_GT(greenLeaves, autumnLeaves) << "mostly green, a touch of autumn";
    EXPECT_GT(autumnLeaves, greenLeaves / 10);
    EXPECT_GT(birchLeaves, 0);
    EXPECT_GT(tulips, 10);
    EXPECT_GT(flowers, cols / 100);
}

TEST(SeasonalForestTest, AutumnLeavesStayInTheBiome) {
    // Turned crowns only grow from seasonal-forest trunks: away from the
    // biome (more than a canopy's reach) there are none.
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    for (int cx = -12; cx < 12; cx += 3)
        for (int cz = -12; cz < 12; cz += 3) {
            GenChunk c(tg, cx, cz);
            bool nearMine = false;
            for (int dx = -8; dx <= CHUNK_SIZE + 8 && !nearMine; dx += 4)
                for (int dz = -8; dz <= CHUNK_SIZE + 8 && !nearMine; dz += 4) {
                    Biome b;
                    tg.getHeightAndBiome(cx * CHUNK_SIZE + dx, cz * CHUNK_SIZE + dz, b);
                    nearMine = b == BIOME_SEASONAL_FOREST;
                }
            if (nearMine) continue;
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++)
                    for (int y = 1; y < CHUNK_HEIGHT; y++)
                        ASSERT_FALSE(isAutumnLeaves(c.at(x, y, z))) << cx << "," << cz;
        }
}

TEST(SeasonalForestTest, CrownsContinueAcrossChunkBorders) {
    // A trunk two blocks from the +X edge: its radius-2 crown layer must
    // continue in the next chunk's first column (the tree is grown from
    // both sides, its leaf colours from world position).
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    int checked = 0;
    for (int cx = CX0; cx < CX1; cx++)
        for (int cz = CZ0; cz < CZ1; cz++) {
            GenChunk a(tg, cx, cz);
            const int x = CHUNK_SIZE - 2;
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (a.biomes[x][z] != BIOME_SEASONAL_FOREST) continue;
                const int h = a.heights[x][z];
                if (a.at(x, h, z) != GRASS || a.at(x, h + 1, z) != WOOD) continue;
                int top = h + 1;
                while (top + 1 < CHUNK_HEIGHT && a.at(x, top + 1, z) == WOOD) top++;
                if (top - h < 4) continue; // a bush, not a tree
                GenChunk b(tg, cx + 1, cz);
                const block_type across = b.at(0, top - 1, z);
                if (across != AIR && !isLeaves(across)) continue; // another trunk or a hillside
                EXPECT_TRUE(isLeaves(across)) << "crown cut at the border, trunk at world (" << cx * CHUNK_SIZE + x << ", "
                    << cz * CHUNK_SIZE + z << ")";
                checked++;
            }
        }
    EXPECT_GT(checked, 0) << "no trunk next to a border in the sample";
}
