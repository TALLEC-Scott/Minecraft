//
// Created by scott on 03/07/23.
//
#pragma once
#include "cube.h"
#include "perlin_noise.h"

struct BiomeParams {
    block_type surfaceBlock;
    block_type subsurfaceBlock;
    float baseHeight;
    float amplitudeFactor;
    float treeDensity; // 0..1
    float treeChance;  // 0..100
};

class TerrainGenerator {
  public:
    TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight);

    int getHeight(int x, int y);
    Biome getBiome(int x, int y);
    // Combined: avoids recomputing the shared climate noises
    int getHeightAndBiome(int x, int y, Biome& outBiome);
    double getTemperature(int x, int y);
    double getMoisture(int x, int y);
    const BiomeParams& getBiomeParams(Biome b);
    double getNoise(int x, int y);
    double getNoise(int x, int y, int z);

    // True if world-space block (wx, wy, wz) — wy vertical — falls inside
    // a cave volume. Purely a density query: callers decide what may be
    // carved (chunk generation guards bedrock, the surface skin, and the
    // ocean floor).
    bool isCave(int wx, int wy, int wz);

  private:
    // Everything derived from one column sample. Height and biome both
    // come out of this so the two public paths can never disagree.
    struct ColumnSample {
        double continentalness; // -1..1, distance from coast
        double erosion;         // -1..1, high = flat terrain
        double peaksValleys;    // -1..1, folded weirdness (ridges/valleys)
        int height;
        Biome biome;
    };
    ColumnSample sampleColumn(int x, int y);

    double octaveNoise(double x, double y, int octaves, double persistence, double lacunarity);
    PerlinNoise perlinNoise;
    float scale;
    int minHeight, maxHeight;

    static const BiomeParams BIOME_TABLE[BIOME_COUNT];
};
