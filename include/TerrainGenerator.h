//
// Created by scott on 03/07/23.
//
#pragma once
#include "cube.h"
#include "perlin_noise.h"

struct BiomeParams {
    block_type surfaceBlock;
    block_type subsurfaceBlock;
    float treeDensity; // 0..1: chance that a tree-grid cell grows a tree
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
    // Raw 2D Perlin offset to [-0.5, 1.5] (noise + 0.5). Only the cloud
    // layer uses it, and its 0.65 coverage threshold is tuned to this range.
    double getNoise(int x, int y);

    unsigned int getSeed() const { return seed; }
    int getSeaLevel() const { return maxHeight / 2; }

    // True if world-space block (wx, wy, wz) — wy vertical — falls inside
    // a cave volume. Purely a density query: callers decide what may be
    // carved (chunk generation guards bedrock, the surface crust, and the
    // ocean floor).
    bool isCave(int wx, int wy, int wz);
    // Caves may breach the surface crust only where this is true — rare,
    // patchy zones, so the ground isn't riddled with holes.
    bool caveEntranceAllowed(int x, int z);
    // Coal ore: compact 3D blobs (~1% of stone), not noise contour sheets.
    bool isCoalOre(int wx, int wy, int wz);
    // Soil depth (dirt / sand) under the surface block, 3..5 blocks.
    int soilDepth(int x, int z);

  private:
    // Everything derived from one column sample. Height and biome both
    // come out of this so the two public paths can never disagree.
    struct ColumnSample {
        double continentalness; // -1..1, distance from coast
        double erosion;         // -1..1, high = flat terrain
        double ridge;           // 0..1, ridged noise: 1 on mountain crest lines
        double relief;          // blocks of mountain relief added at this column
        int height;
        Biome biome;
    };
    ColumnSample sampleColumn(int x, int y);

    double octaveNoise(double x, double y, int octaves, double persistence, double lacunarity);
    // Ridged multifractal: 1 - |noise| per octave, squared, so crests form
    // connected lines (ridgelines / valleys) instead of round blobs.
    double ridgedNoise(double x, double y, int octaves);
    PerlinNoise perlinNoise;
    unsigned int seed;
    float scale;
    int minHeight, maxHeight;

    static const BiomeParams BIOME_TABLE[BIOME_COUNT];
};
