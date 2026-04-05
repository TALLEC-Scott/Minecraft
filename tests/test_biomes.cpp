#include <gtest/gtest.h>
#include "TerrainGenerator.h"
#include "cube.h"

class BiomeTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{0, 0.1f, 0, CHUNK_HEIGHT};
};

TEST_F(BiomeTest, BiomeInValidRange) {
    for (int x = -200; x < 200; x += 13) {
        for (int z = -200; z < 200; z += 13) {
            Biome b = terrain.getBiome(x, z);
            EXPECT_GE(b, 0);
            EXPECT_LT(b, BIOME_COUNT);
        }
    }
}

TEST_F(BiomeTest, AllBiomesExistSomewhere) {
    // Scan a large area and verify every biome type appears at least once
    bool found[BIOME_COUNT] = {};
    for (int x = -2000; x < 2000; x += 7) {
        for (int z = -2000; z < 2000; z += 7) {
            found[terrain.getBiome(x, z)] = true;
        }
    }
    const char* names[] = {"Ocean", "Beach", "Plains", "Forest", "Desert", "Tundra"};
    for (int i = 0; i < BIOME_COUNT; i++) {
        EXPECT_TRUE(found[i]) << "Biome " << names[i] << " not found in 4000x4000 area";
    }
}

TEST_F(BiomeTest, OceanHasLowContinentalness) {
    // Check that ocean biomes correspond to low terrain height (below sea level)
    int oceanCount = 0;
    int oceanBelowSea = 0;
    int seaLevel = CHUNK_HEIGHT / 2;

    for (int x = -1000; x < 1000; x += 11) {
        for (int z = -1000; z < 1000; z += 11) {
            if (terrain.getBiome(x, z) == BIOME_OCEAN) {
                oceanCount++;
                if (terrain.getHeight(x, z) <= seaLevel) oceanBelowSea++;
            }
        }
    }

    if (oceanCount > 0) {
        double ratio = (double)oceanBelowSea / oceanCount;
        EXPECT_GT(ratio, 0.9) << "Most ocean blocks should be below sea level";
    }
}

TEST_F(BiomeTest, BiomeDeterministic) {
    for (int x = -50; x < 50; x += 10) {
        for (int z = -50; z < 50; z += 10) {
            EXPECT_EQ(terrain.getBiome(x, z), terrain.getBiome(x, z));
        }
    }
}

TEST_F(BiomeTest, GetHeightAndBiomeConsistent) {
    for (int x = -100; x < 100; x += 17) {
        for (int z = -100; z < 100; z += 17) {
            Biome biome1;
            int h1 = terrain.getHeightAndBiome(x, z, biome1);
            Biome biome2 = terrain.getBiome(x, z);
            int h2 = terrain.getHeight(x, z);
            EXPECT_EQ(h1, h2);
            EXPECT_EQ(biome1, biome2);
        }
    }
}

TEST_F(BiomeTest, BiomeParamsValid) {
    for (int i = 0; i < BIOME_COUNT; i++) {
        const BiomeParams& bp = terrain.getBiomeParams(static_cast<Biome>(i));
        EXPECT_GE(bp.treeDensity, 0.0f);
        EXPECT_LE(bp.treeDensity, 1.0f);
        EXPECT_GE(bp.treeChance, 0.0f);
        EXPECT_LE(bp.treeChance, 100.0f);
    }
}
