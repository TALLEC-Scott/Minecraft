//
// Created by scott on 03/07/23.
//
#include "TerrainGenerator.h"

TerrainGenerator::TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight)
        : perlinNoise(seed), scale(scale), minHeight(minHeight), maxHeight(maxHeight) {}

int TerrainGenerator::getHeight(int x, int y) {
    // Generate noise value (-1 to 1)
    double noise = perlinNoise.noise(x * scale, y * scale);

    // Scale and bias the value into the desired range
    return minHeight + static_cast<int>((noise + 1.0) * 0.5 * (maxHeight - minHeight));
}