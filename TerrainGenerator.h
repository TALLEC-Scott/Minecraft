//
// Created by scott on 03/07/23.
//
#pragma once
#include "perlin_noise.h"

class TerrainGenerator {
public:
    TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight);

    int getHeight(int x, int y);
    double getNoise(int x, int y);
    double getNoise(int x, int y, int z);

private:
    PerlinNoise perlinNoise;
    float scale;
    int minHeight, maxHeight;

};