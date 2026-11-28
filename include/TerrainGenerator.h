//
// Created by scott on 03/07/23.
//
#pragma once
#include "cube.h"
#include "perlin_noise.h"

class TerrainGenerator {
  public:
    TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight);

    int getHeight(int x, int y);
    Biome getBiome(int x, int y);
    // Combined: avoids recomputing the shared climate noises
    int getHeightAndBiome(int x, int y, Biome& outBiome);
    double getTemperature(int x, int y);
    double getMoisture(int x, int y);

    // Heightmap height plus "jaggedness" (0..1): how strongly 3D overhang
    // density reshapes the ground around that height (terrain_density.h).
    // 0 on plains, near water, on beaches / ocean and along rivers; up to 1
    // on mountains and cliff land. No biome lookup (cheaper than
    // getHeightAndBiome); `jagged` of the combined call below matches it.
    struct ColumnShape {
        int height;
        float jagged;
    };
    ColumnShape getShape(int x, int z);
    int getHeightAndBiome(int x, int z, Biome& outBiome, float& outJagged);
    // Overhang noise at world block (x, y, z), roughly -1..1: two octaves of
    // 3D Perlin, stretched horizontally so cuts form ledges and shelves.
    double overhangNoise(int x, int y, int z);
    // The climate a column's vegetation feels: temperature after altitude
    // cooling (the same `cooled` pickBiome uses for snow caps, so mountains
    // get paler, sparser grass toward the tundra) and humidity, both 0..1.
    struct ColumnClimate {
        double temperature;
        double humidity;
    };
    // Height, biome and (if `climate` is given) the column's climate in one
    // sample: land columns compute the climate for their biome anyway, so
    // it's free there; ocean / beach columns pay for it only when asked.
    // `jagged` (if given) gets the column's jaggedness, as getShape.
    int getHeightBiomeClimate(int x, int y, Biome& outBiome, ColumnClimate* climate, float* jagged = nullptr);
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
    // Coal ore: compact 3D blobs, not noise contour sheets. ~1.3% of stone
    // up to sea level, rising with altitude to ~5% (bigger veins) on peaks.
    bool isCoalOre(int wx, int wy, int wz);
    // Iron (y ≤ IRON_MAX_Y) and diamond (y ≤ DIAMOND_MAX_Y): smaller,
    // rarer blobs from their own noises.
    bool isIronOre(int wx, int wy, int wz);
    bool isDiamondOre(int wx, int wy, int wz);
    // What a stone cell holds: an ore, or STONE. Coal wins over iron over
    // diamond where blobs overlap.
    block_type oreAt(int wx, int wy, int wz);
    static constexpr int IRON_MAX_Y = 64;
    static constexpr int DIAMOND_MAX_Y = 16;
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
        float jagged;
        Biome biome;
        ColumnClimate climate; // set on land, or when asked for (sampleColumn)
    };
    ColumnSample sampleColumn(int x, int y, bool wantClimate = false, bool withBiome = true);

    double octaveNoise(double x, double y, int octaves, double persistence, double lacunarity);
    // Ridged multifractal: 1 - |noise| per octave, squared, so crests form
    // connected lines (ridgelines / valleys) instead of round blobs.
    double ridgedNoise(double x, double y, int octaves);
    PerlinNoise perlinNoise;
    unsigned int seed;
    float scale;
    int minHeight, maxHeight;
};
