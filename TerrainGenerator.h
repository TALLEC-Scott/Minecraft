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
    float treeDensity;   // 0..1
    float treeChance;    // 0..100
};

class TerrainGenerator {
public:
    TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight);

    int getHeight(int x, int y);
    Biome getBiome(int x, int y);
    double getTemperature(int x, int y);
    double getMoisture(int x, int y);
    const BiomeParams& getBiomeParams(Biome b);
    double getNoise(int x, int y);
    double getNoise(int x, int y, int z);

private:
    double octaveNoise(double x, double y, int octaves, double persistence, double lacunarity);
    double getContinental(double nx, double ny);
    PerlinNoise perlinNoise;
    float scale;
    int minHeight, maxHeight;

    static const BiomeParams BIOME_TABLE[BIOME_COUNT];
};