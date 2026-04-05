#include <gtest/gtest.h>
#include "TerrainGenerator.h"
#include "cube.h"

class TerrainTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{0, 0.1f, 0, CHUNK_HEIGHT};
};

TEST_F(TerrainTest, HeightWithinBounds) {
    for (int x = -100; x < 100; x += 7) {
        for (int z = -100; z < 100; z += 7) {
            int h = terrain.getHeight(x, z);
            EXPECT_GE(h, 0) << "at (" << x << ", " << z << ")";
            EXPECT_LT(h, CHUNK_HEIGHT) << "at (" << x << ", " << z << ")";
        }
    }
}

TEST_F(TerrainTest, DeterministicWithSameSeed) {
    TerrainGenerator t1(42, 0.1f, 0, CHUNK_HEIGHT);
    TerrainGenerator t2(42, 0.1f, 0, CHUNK_HEIGHT);

    for (int x = 0; x < 50; x += 5) {
        for (int z = 0; z < 50; z += 5) {
            EXPECT_EQ(t1.getHeight(x, z), t2.getHeight(x, z));
        }
    }
}

TEST_F(TerrainTest, DifferentSeedsDifferentTerrain) {
    TerrainGenerator t1(0, 0.1f, 0, CHUNK_HEIGHT);
    TerrainGenerator t2(999, 0.1f, 0, CHUNK_HEIGHT);

    int differences = 0;
    for (int x = 0; x < 50; x += 5) {
        for (int z = 0; z < 50; z += 5) {
            if (t1.getHeight(x, z) != t2.getHeight(x, z)) differences++;
        }
    }
    EXPECT_GT(differences, 0);
}

TEST_F(TerrainTest, MoistureInRange) {
    for (int x = -50; x < 50; x += 10) {
        for (int z = -50; z < 50; z += 10) {
            double m = terrain.getMoisture(x, z);
            EXPECT_GE(m, 0.0) << "at (" << x << ", " << z << ")";
            EXPECT_LE(m, 1.0) << "at (" << x << ", " << z << ")";
        }
    }
}

TEST_F(TerrainTest, TemperatureInRange) {
    for (int x = -50; x < 50; x += 10) {
        for (int z = -50; z < 50; z += 10) {
            double t = terrain.getTemperature(x, z);
            EXPECT_GE(t, 0.0) << "at (" << x << ", " << z << ")";
            EXPECT_LE(t, 1.0) << "at (" << x << ", " << z << ")";
        }
    }
}

TEST_F(TerrainTest, HeightAndBiomeCombinedMatchesSeparate) {
    for (int x = -30; x < 30; x += 7) {
        for (int z = -30; z < 30; z += 7) {
            Biome biome;
            int combinedH = terrain.getHeightAndBiome(x, z, biome);
            int separateH = terrain.getHeight(x, z);
            Biome separateB = terrain.getBiome(x, z);
            EXPECT_EQ(combinedH, separateH) << "at (" << x << ", " << z << ")";
            EXPECT_EQ(biome, separateB) << "at (" << x << ", " << z << ")";
        }
    }
}
