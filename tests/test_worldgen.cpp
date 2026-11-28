// Tests for chunk world generation (fillChunkTerrain, src/terrain_fill.cpp)
// and the terrain shape it draws from (TerrainGenerator). GL-free.

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

#include "TerrainGenerator.h"
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
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

class WorldgenTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
};

} // namespace

TEST_F(WorldgenTest, Deterministic) {
    TerrainGenerator other(42, 0.1f, 0, CHUNK_HEIGHT);
    GenChunk a(terrain, 3, -2), b(other, 3, -2);
    for (size_t i = 0; i < VOLUME; i++) ASSERT_EQ(a.blocks[i].getType(), b.blocks[i].getType()) << i;
}

TEST_F(WorldgenTest, SoilIsAThinLayer) {
    // Under a grass surface: 2-4 dirt blocks, then stone (the old generator
    // laid ~20 blocks of dirt over everything).
    int grassCols = 0;
    for (int cx = -4; cx < 4; cx += 2)
        for (int cz = -4; cz < 4; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int h = c.heights[x][z];
                    if (c.at(x, h, z) != GRASS) continue;
                    grassCols++;
                    int dirt = 0;
                    for (int y = h - 1; y > 0 && c.at(x, y, z) == DIRT; y--) dirt++;
                    EXPECT_LE(dirt, 4) << "at (" << cx * CHUNK_SIZE + x << ", " << cz * CHUNK_SIZE + z << ")";
                }
        }
    EXPECT_GT(grassCols, 100);
}

TEST_F(WorldgenTest, CavesRarelyBreachTheSurface) {
    int land = 0, open = 0;
    for (int cx = -8; cx < 8; cx += 2)
        for (int cz = -8; cz < 8; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int h = c.heights[x][z];
                    if (h <= SEA) continue;
                    land++;
                    if (c.at(x, h, z) == AIR) open++;
                }
        }
    ASSERT_GT(land, 1000);
    EXPECT_LT(static_cast<double>(open) / land, 0.02) << open << " of " << land << " land columns open";
}

TEST_F(WorldgenTest, CoalIsSparseOre) {
    long solid = 0, coal = 0;
    for (int cx = -6; cx < 6; cx += 3)
        for (int cz = -6; cz < 6; cz += 3) {
            GenChunk c(terrain, cx, cz);
            for (size_t i = 0; i < VOLUME; i++) {
                block_type t = c.blocks[i].getType();
                if (t == STONE || t == COAL_ORE || t == IRON_ORE || t == DIAMOND_ORE) solid++;
                if (t == COAL_ORE) coal++;
            }
        }
    double frac = static_cast<double>(coal) / solid;
    EXPECT_GT(frac, 0.003);
    EXPECT_LT(frac, 0.025);
}

TEST_F(WorldgenTest, SurfaceRulesFollowSlopeAndAltitude) {
    int cliffs = 0, snowy = 0;
    for (int cx = -10; cx < 10; cx += 2)
        for (int cz = -10; cz < 10; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 1; x < CHUNK_SIZE - 1; x++)
                for (int z = 1; z < CHUNK_SIZE - 1; z++) {
                    int h = c.heights[x][z];
                    if (h <= SEA + 1) continue; // shores are sand by design
                    block_type top = c.at(x, h, z);
                    if (top == AIR) continue;   // cave entrance
                    int slope = 0;
                    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                        slope = std::max(slope, std::abs(c.heights[x + dx][z + dz] - h));
                    if (slope >= terrainfill::CLIFF_SLOPE) {
                        cliffs++;
                        // Bare rock, showing its ore like any stone.
                        EXPECT_TRUE(top == STONE || top == COAL_ORE || top == IRON_ORE || top == DIAMOND_ORE)
                            << "cliff face should be bare rock";
                    } else if (h - SEA >= terrainfill::SNOW_LINE) {
                        snowy++;
                        EXPECT_EQ(top, SNOW) << "above the snow line";
                    }
                }
        }
    EXPECT_GT(cliffs, 0) << "no cliffs in a 320×320 area";
}

TEST_F(WorldgenTest, TerrainHasMountainsCliffsAndRivers) {
    int maxH = 0, bigSteps = 0, riverCols = 0;
    for (int x = -1024; x < 1024; x += 2) {
        int prev = terrain.getHeight(x, 0);
        for (int z = -1024; z < 1024; z += 2) {
            Biome b;
            int h = terrain.getHeightAndBiome(x, z, b);
            maxH = std::max(maxH, h);
            // Land biome below sea level = carved river channel.
            if (h < SEA - 1 && b != BIOME_OCEAN && b != BIOME_BEACH) riverCols++;
            (void)prev;
        }
    }
    // Cliffs: adjacent-column steps of more than 4 blocks exist.
    for (int x = -512; x < 512; x++) {
        int prev = terrain.getHeight(x, -512);
        for (int z = -511; z < 512; z++) {
            int h = terrain.getHeight(x, z);
            if (std::abs(h - prev) > 4) bigSteps++;
            prev = h;
        }
        if (bigSteps > 50) break;
    }
    EXPECT_GE(maxH, SEA + 35) << "no mountains";
    EXPECT_GT(bigSteps, 50) << "no cliffs";
    EXPECT_GT(riverCols, 100) << "no river channels";
}

TEST_F(WorldgenTest, BiomesFormRegionsNotSpeckle) {
    // Walk three long lines and measure land blocks per biome change. The
    // old 150-block climate blobs scored ~54 here (seed 42); the larger
    // climate scale roughly doubles it (~90). Coasts and snowy peaks keep
    // some short runs, so the bar sits between the two. Taiga and birch
    // forest add real borders between the old regions (~72 with them), so
    // the bar sits at 65: still well clear of the speckled 54.
    int changes = 0, landSamples = 0;
    for (int zLine : {700, -900, 2500}) {
        Biome prev = terrain.getBiome(-3000, zLine);
        for (int x = -3000; x < 3000; x++) {
            Biome b = terrain.getBiome(x, zLine);
            if (b != BIOME_OCEAN) landSamples++;
            if (b != prev) changes++;
            prev = b;
        }
    }
    ASSERT_GT(landSamples, 1500);
    EXPECT_GT(static_cast<double>(landSamples) / std::max(1, changes), 65.0);
}

TEST_F(WorldgenTest, TreeCanopiesCrossChunkBorders) {
    // Find a trunk two columns from a chunk's +X edge (trunks sit at offsets
    // 0-2 inside 4-block cells, so x = 15 never holds one). Its canopy layer
    // at the trunk top spans ≥ 2 blocks, so it must continue into the +X
    // neighbor's first column.
    int checked = 0;
    for (int cx = -12; cx < 12 && checked < 3; cx++)
        for (int cz = -12; cz < 12 && checked < 3; cz++) {
            GenChunk a(terrain, cx, cz);
            for (int z = 0; z < CHUNK_SIZE && checked < 3; z++) {
                int x = CHUNK_SIZE - 2;
                int h = a.heights[x][z];
                if (h + 1 >= CHUNK_HEIGHT || a.at(x, h + 1, z) != WOOD) continue;
                int top = h + 1;
                while (top + 1 < CHUNK_HEIGHT && a.at(x, top + 1, z) == WOOD) top++;
                GenChunk b(terrain, cx + 1, cz);
                block_type t = b.at(0, top, z);
                // Only oak trunks are picked (spruce tops are 1 wide); a
                // neighbouring birch canopy may fill the cell instead.
                EXPECT_TRUE(isLeaves(t) || isLog(t))
                    << "canopy cut at chunk border, trunk at world (" << cx * CHUNK_SIZE + x << ", "
                    << cz * CHUNK_SIZE + z << ")";
                checked++;
            }
        }
    EXPECT_GT(checked, 0) << "no border trunk found to check";
}

TEST_F(WorldgenTest, ForestsAreDense) {
    // Count trunks in forest columns: ~0.6 per 4×4 cell → several per chunk.
    long forestCols = 0, trunks = 0;
    for (int cx = -16; cx < 16; cx += 2)
        for (int cz = -16; cz < 16; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (c.biomes[x][z] != BIOME_FOREST) continue;
                    forestCols++;
                    int h = c.heights[x][z];
                    if (h + 1 < CHUNK_HEIGHT && isLog(c.at(x, h + 1, z))) trunks++;
                }
        }
    ASSERT_GT(forestCols, 256) << "no forest in the sample";
    EXPECT_GT(256.0 * trunks / forestCols, 4.0) << "trees per forest chunk";
}

TEST_F(WorldgenTest, OresStayInTheirDepthBands) {
    long stone = 0, iron = 0, diamond = 0, coal = 0;
    for (int cx = -6; cx < 6; cx += 2)
        for (int cz = -6; cz < 6; cz += 2) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++)
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        block_type t = c.at(x, y, z);
                        if (t == STONE || t == COAL_ORE || t == IRON_ORE || t == DIAMOND_ORE) stone++;
                        if (t == COAL_ORE) coal++;
                        if (t == IRON_ORE) {
                            ASSERT_LE(y, TerrainGenerator::IRON_MAX_Y);
                            iron++;
                        }
                        if (t == DIAMOND_ORE) {
                            ASSERT_LE(y, TerrainGenerator::DIAMOND_MAX_Y);
                            diamond++;
                        }
                    }
        }
    EXPECT_GT(iron, 0);
    EXPECT_GT(diamond, 0);
    EXPECT_LT(iron, coal) << "iron should be rarer than coal";
    EXPECT_LT(diamond, iron) << "diamond should be rarer than iron";
    EXPECT_LT(static_cast<double>(diamond) / stone, 0.002);
}

TEST_F(WorldgenTest, BiomesGrowTheirOwnTrees) {
    // Trunk bases (the log just above the surface) by biome: taiga and tundra
    // grow only spruce, forests mostly oak with some birch.
    long spruceInTaiga = 0, otherInTaiga = 0, birchInForest = 0, oakInForest = 0;
    for (int cx = -40; cx < 40; cx += 4)
        for (int cz = -40; cz < 40; cz += 4) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int h = c.heights[x][z];
                    if (h + 1 >= CHUNK_HEIGHT) continue;
                    block_type t = c.at(x, h + 1, z);
                    if (!isLog(t)) continue;
                    Biome b = c.biomes[x][z];
                    if (b == BIOME_TAIGA || b == BIOME_TUNDRA) (t == SPRUCE_LOG ? spruceInTaiga : otherInTaiga)++;
                    if (b == BIOME_FOREST) {
                        if (t == BIRCH_LOG) birchInForest++;
                        if (t == WOOD) oakInForest++;
                    }
                }
        }
    EXPECT_GT(spruceInTaiga, 10);
    EXPECT_EQ(otherInTaiga, 0);
    EXPECT_GT(birchInForest, 5);
    EXPECT_GT(oakInForest, birchInForest);
}

TEST_F(WorldgenTest, DeepCavesHoldLavaAndShallowOnesDont) {
    // Minecraft's lava level: cave space at y <= 10 is lava, never above it.
    int lava = 0;
    for (int cx = -6; cx < 6; cx++)
        for (int cz = -6; cz < 6; cz++) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++)
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        if (c.at(x, y, z) != LAVA) continue;
                        ASSERT_LE(y, 10) << "lava above the lava level at " << cx << "," << cz;
                        lava++;
                    }
        }
    EXPECT_GT(lava, 0) << "some deep cave should hold lava";
}
