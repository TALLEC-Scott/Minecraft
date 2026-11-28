// Swampland (src/biomes/swampland.cpp): shallow pools (some clay-floored)
// stay on swamp columns, swamp oaks hang strands of leaves from a canopy
// within TREE_MAX_RADIUS, and blue orchids grow only in swamps.

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

#include "TerrainGenerator.h"
#include "biome.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr int SEA = CHUNK_HEIGHT / 2;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int cx, int cz) { fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes); }
    block_type at(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// Chunks well inside a swamp for seed 42 (around world (912, 496)).
constexpr int SWAMP_CX = 57, SWAMP_CZ = 31;

class SwamplandTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
};

} // namespace

TEST_F(SwamplandTest, RegionIsSwamp) {
    GenChunk c(terrain, SWAMP_CX, SWAMP_CZ);
    int swamp = 0;
    for (auto& row : c.biomes)
        for (Biome b : row) swamp += b == BIOME_SWAMPLAND;
    EXPECT_GT(swamp, CHUNK_SIZE * CHUNK_SIZE * 3 / 4);
}

TEST_F(SwamplandTest, PoolsAreShallowAndCommon) {
    long cols = 0, pools = 0, clay = 0;
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            GenChunk c(terrain, SWAMP_CX + dx, SWAMP_CZ + dz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (c.biomes[x][z] != BIOME_SWAMPLAND) continue;
                    const int h = c.heights[x][z];
                    if (h <= SEA) continue;
                    cols++;
                    if (c.at(x, h, z) != WATER) continue;
                    pools++;
                    // One block deep, open sky above it.
                    EXPECT_NE(c.at(x, h - 1, z), WATER);
                    EXPECT_NE(c.at(x, h - 1, z), AIR);
                    EXPECT_EQ(c.at(x, h + 1, z), AIR);
                    if (c.at(x, h - 1, z) == CLAY) clay++;
                }
        }
    ASSERT_GT(cols, 1000);
    EXPECT_GT(pools, cols / 20);
    EXPECT_LT(pools, cols / 3);
    EXPECT_GT(clay, 0);
}

TEST_F(SwamplandTest, PoolsAndOrchidsStayInSwamps) {
    long swampPools = 0, strayWater = 0, orchids = 0, strayOrchids = 0;
    for (int cx = -30; cx < 70; cx += 3)
        for (int cz = -30; cz < 40; cz += 3) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    if (h <= SEA || h + 1 >= CHUNK_HEIGHT) continue;
                    const bool swamp = c.biomes[x][z] == BIOME_SWAMPLAND;
                    if (c.at(x, h + 1, z) == BLUE_ORCHID) (swamp ? orchids : strayOrchids)++;
                    if (c.at(x, h, z) != WATER || c.at(x, h, z) == c.at(x, SEA, z)) continue;
                    // Water at the surface above sea level: a swamp pool.
                    if (swamp) {
                        swampPools++;
                    } else if (c.at(x, h - 1, z) == CLAY) {
                        strayWater++;
                    }
                }
        }
    EXPECT_GT(swampPools, 0);
    EXPECT_GT(orchids, 0);
    EXPECT_EQ(strayOrchids, 0);
    EXPECT_EQ(strayWater, 0);
}

TEST_F(SwamplandTest, SwampOaksHangLeavesAndStayInRadius) {
    long trees = 0, strands = 0;
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            GenChunk c(terrain, SWAMP_CX + dx, SWAMP_CZ + dz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    if (c.biomes[x][z] != BIOME_SWAMPLAND || c.at(x, h + 1, z) != WOOD) continue;
                    trees++;
                    int top = h + 1;
                    while (c.at(x, top + 1, z) == WOOD) top++;
                    EXPECT_LE(top - h, 6);
                    // Rim strands: leaves 3 out from the trunk, below the
                    // canopy's lowest layer (top - 2).
                    for (int ox = -3; ox <= 3; ox++)
                        for (int oz = -3; oz <= 3; oz++) {
                            if (std::abs(ox) != 3 && std::abs(oz) != 3) continue;
                            const int lx = x + ox, lz = z + oz;
                            if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;
                            if (c.at(lx, top - 3, lz) == LEAVES) strands++;
                        }
                }
        }
    EXPECT_GT(trees, 5);
    EXPECT_GT(strands, trees);
}

TEST_F(SwamplandTest, TintsAreMurkierThanForest) {
    const BiomeDef& swamp = biomeDef(BIOME_SWAMPLAND);
    const BiomeDef& forest = biomeDef(BIOME_FOREST);
    // Less green than the forest, warmer (olive): more red relative to green.
    EXPECT_LT(swamp.grassTint.g, forest.grassTint.g);
    EXPECT_LT(swamp.foliageTint.g, forest.foliageTint.g);
    EXPECT_GT(swamp.grassTint.r / swamp.grassTint.g, forest.grassTint.r / forest.grassTint.g);
}
