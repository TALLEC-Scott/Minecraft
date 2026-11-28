// Savanna (src/biomes/savanna.cpp): acacias grow only in savanna, their
// canopies stay thin and within TREE_MAX_RADIUS of the trunk, and the grass
// is thick with tall grass.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "TerrainGenerator.h"
#include "biome.h"
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

// Chunks well inside a savanna for seed 42 (around world (-1160, 760)).
constexpr int SAV_CX = -73, SAV_CZ = 47;

class SavannaTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
};

} // namespace

TEST_F(SavannaTest, RegionIsSavanna) {
    GenChunk c(terrain, SAV_CX, SAV_CZ);
    int sav = 0;
    for (auto& row : c.biomes)
        for (Biome b : row) sav += b == BIOME_SAVANNA;
    EXPECT_GT(sav, CHUNK_SIZE * CHUNK_SIZE * 3 / 4);
}

TEST_F(SavannaTest, AcaciasGrowInSavannaOnly) {
    long acaciaTrunks = 0, acaciaElsewhere = 0, oakInSavanna = 0;
    for (int cx = -40; cx < 40; cx += 3)
        for (int cz = -40; cz < 40; cz += 3) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int h = c.heights[x][z];
                    if (h + 1 >= CHUNK_HEIGHT) continue;
                    block_type t = c.at(x, h + 1, z);
                    if (t == ACACIA_LOG) (c.biomes[x][z] == BIOME_SAVANNA ? acaciaTrunks : acaciaElsewhere)++;
                    if (t == WOOD && c.biomes[x][z] == BIOME_SAVANNA) oakInSavanna++;
                }
        }
    EXPECT_GT(acaciaTrunks, 4);
    // A trunk base right at a border can stand on a neighbour's column
    // (the tree's own column was savanna); never more than a handful.
    EXPECT_LE(acaciaElsewhere, acaciaTrunks / 10);
    EXPECT_LT(oakInSavanna, acaciaTrunks);
}

TEST_F(SavannaTest, TreesAreSparseAndGrassIsThick) {
    long cols = 0, grass = 0, trunks = 0;
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            GenChunk c(terrain, SAV_CX + dx, SAV_CZ + dz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (c.biomes[x][z] != BIOME_SAVANNA) continue;
                    int h = c.heights[x][z];
                    if (c.at(x, h, z) != GRASS) continue;
                    cols++;
                    block_type t = c.at(x, h + 1, z);
                    grass += t == TALL_GRASS;
                    trunks += isLog(t);
                }
        }
    ASSERT_GT(cols, 3000);
    EXPECT_GT(static_cast<double>(grass) / static_cast<double>(cols), 0.2);
    // A few trees per chunk at most (25 chunks).
    EXPECT_GT(trunks, 5);
    EXPECT_LT(trunks, 25 * 5);
}

TEST_F(SavannaTest, CanopiesStayWithinReachOfATrunk) {
    // Every acacia block in a chunk must lie within TREE_MAX_RADIUS of an
    // acacia trunk base, in this chunk or a neighbour: a canopy drawn by a
    // chunk whose trunk stands next door is the same tree.
    long acaciaBlocks = 0;
    for (int ox = -2; ox <= 2; ox++)
        for (int oz = -2; oz <= 2; oz++) {
            GenChunk c(terrain, SAV_CX + ox, SAV_CZ + oz);
            // Trunk bases within this chunk and its 8 neighbours.
            std::vector<std::pair<int, int>> bases;
            for (int nx = -1; nx <= 1; nx++)
                for (int nz = -1; nz <= 1; nz++) {
                    GenChunk n(terrain, SAV_CX + ox + nx, SAV_CZ + oz + nz);
                    for (int x = 0; x < CHUNK_SIZE; x++)
                        for (int z = 0; z < CHUNK_SIZE; z++) {
                            int h = n.heights[x][z];
                            if (n.at(x, h + 1, z) == ACACIA_LOG && n.at(x, h, z) != ACACIA_LOG)
                                bases.emplace_back(nx * CHUNK_SIZE + x, nz * CHUNK_SIZE + z);
                        }
                }
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++)
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        block_type t = c.at(x, y, z);
                        if (t != ACACIA_LOG && t != ACACIA_LEAVES) continue;
                        acaciaBlocks++;
                        bool near = false;
                        for (auto [bx, bz] : bases)
                            if (std::abs(bx - x) <= biomegen::TREE_MAX_RADIUS &&
                                std::abs(bz - z) <= biomegen::TREE_MAX_RADIUS)
                                near = true;
                        ASSERT_TRUE(near) << "acacia block at " << x << "," << y << "," << z;
                    }
        }
    EXPECT_GT(acaciaBlocks, 50);
}

TEST_F(SavannaTest, AcaciaCanopiesAreFlat) {
    // Acacia leaves come in thin layers: a tree never stacks more than 2
    // leaf blocks; only where two neighbouring canopies overlap may a
    // column hold a thicker pile, and that stays rare.
    long leafCols = 0, thick = 0;
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            GenChunk c(terrain, SAV_CX + dx, SAV_CZ + dz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int run = 0, longest = 0;
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        run = c.at(x, y, z) == ACACIA_LEAVES ? run + 1 : 0;
                        longest = std::max(longest, run);
                    }
                    if (longest == 0) continue;
                    leafCols++;
                    thick += longest > 2;
                }
        }
    ASSERT_GT(leafCols, 100);
    EXPECT_LT(static_cast<double>(thick) / static_cast<double>(leafCols), 0.05) << thick << " of " << leafCols;
}
