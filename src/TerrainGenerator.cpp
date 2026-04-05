//
// Created by scott on 03/07/23.
//
#include "TerrainGenerator.h"
#include <algorithm>

const BiomeParams TerrainGenerator::BIOME_TABLE[BIOME_COUNT] = {
    // surface       subsurface   baseH  amp   treeDens  treeChance
    {SAND,           SAND,         0.0f, 0.0f,  0.0f,    0.0f},  // OCEAN (height handled specially)
    {SAND,           SAND,         0.0f, 0.0f,  0.0f,    0.0f},  // BEACH (height handled specially)
    {GRASS,          DIRT,         0.0f, 0.0f,  0.15f,  20.0f},  // PLAINS
    {GRASS,          DIRT,         0.0f, 0.0f,  0.8f,   50.0f},  // FOREST
    {SAND,           SAND,         0.0f, 0.0f,  0.0f,    0.0f},  // DESERT
    {SNOW,           DIRT,         0.0f, 0.0f,  0.0f,    0.0f},  // TUNDRA
};

TerrainGenerator::TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight)
        : perlinNoise(seed), scale(scale), minHeight(minHeight), maxHeight(maxHeight) {}

double TerrainGenerator::octaveNoise(double x, double y, int octaves, double persistence, double lacunarity) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxVal = 0.0;
    for (int i = 0; i < octaves; i++) {
        total += perlinNoise.noise(x * frequency, y * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxVal;
}

double TerrainGenerator::getContinental(double nx, double ny) {
    double c = perlinNoise.noise(nx * 0.006, ny * 0.006);
    return (c + 1.0) * 0.5; // 0..1
}

double TerrainGenerator::getTemperature(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;
    double t = octaveNoise(nx * 0.06 + 2000.0, ny * 0.06 + 2000.0, 2, 0.5, 2.0);
    return (t + 1.0) * 0.5;
}

double TerrainGenerator::getMoisture(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;
    double m = octaveNoise(nx * 0.08 + 1000.0, ny * 0.08 + 1000.0, 2, 0.5, 2.0);
    return (m + 1.0) * 0.5;
}

Biome TerrainGenerator::getBiome(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;
    double c = getContinental(nx, ny);

    // Ocean and beach are geographic, not climate
    if (c < 0.35) return BIOME_OCEAN;
    if (c < 0.42) return BIOME_BEACH;

    // Inland: biome determined by climate (temperature + humidity)
    double temp = getTemperature(x, y);
    double humid = getMoisture(x, y);

    // Desert first: hot + dry, only suppressed at very high continental (mountain peaks)
    if (temp > 0.48 && humid < 0.42 && c < 0.75) return BIOME_DESERT;

    // Higher altitude = colder (applied after desert check)
    double altitudeCooling = std::max(0.0, (c - 0.5) / 0.5) * 0.35;
    temp -= altitudeCooling;

    if (temp < 0.3) return BIOME_TUNDRA;
    if (humid > 0.5) return BIOME_FOREST;
    return BIOME_PLAINS;
}

const BiomeParams& TerrainGenerator::getBiomeParams(Biome b) {
    return BIOME_TABLE[b];
}

int TerrainGenerator::getHeight(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;

    double c = getContinental(nx, ny);
    double detail = (octaveNoise(nx * 0.35, ny * 0.35, 3, 0.45, 2.0) + 1.0) * 0.5;
    double erosion = (octaveNoise(nx * 0.1, ny * 0.1, 3, 0.4, 2.0) + 1.0) * 0.5;

    int seaLevel = maxHeight / 2;
    double roughness = erosion * 0.6 + 0.3;
    double height;

    if (c < 0.35) {
        // Ocean floor
        double depth = c / 0.35;
        height = seaLevel * 0.4 + depth * seaLevel * 0.5;
        height += detail * 3.0 - 1.5;
    } else if (c < 0.45) {
        // Coast transition: smoothstep from ocean edge to land
        double t = (c - 0.35) / 0.1;
        double st = t * t * (3.0 - 2.0 * t);
        double oceanEdge = seaLevel * 0.9;
        double landEdge = seaLevel + 2.0;
        height = oceanEdge + st * (landEdge - oceanEdge);
        height += detail * 2.0 * st;
    } else {
        // Inland biomes: continuous continental value for smooth transitions
        double inland = (c - 0.45) / 0.55; // 0..1
        double ist = inland * inland * (3.0 - 2.0 * inland); // smoothstep

        // Base height ramps from coast to deep inland
        double base = seaLevel + 2.0 + ist * 30.0;
        double amp = 0.3 + ist * 2.0;

        // Ridged noise for mountain peaks: sharp ridges where noise crosses zero
        double ridged = 0.0;
        double ridgeAmp = 1.0;
        double ridgeFreq = 0.4;
        double ridgeMax = 0.0;
        for (int i = 0; i < 3; i++) {
            double n = perlinNoise.noise(nx * ridgeFreq, ny * ridgeFreq);
            ridged += (1.0 - std::abs(n)) * ridgeAmp;
            ridgeMax += ridgeAmp;
            ridgeAmp *= 0.5;
            ridgeFreq *= 2.0;
        }
        ridged /= ridgeMax; // 0..1

        // Blend: plains use smooth detail, mountains use ridged noise
        double shape = detail * (1.0 - ist) + ridged * ist;

        height = base + shape * amp * 30.0 * roughness;
    }

    return std::clamp(static_cast<int>(height), minHeight, maxHeight);
}

int TerrainGenerator::getHeightAndBiome(int x, int y, Biome& outBiome) {
    double nx = x * scale;
    double ny = y * scale;
    double c = getContinental(nx, ny);

    // Compute biome using shared continental value
    if (c < 0.35) {
        outBiome = BIOME_OCEAN;
    } else if (c < 0.42) {
        outBiome = BIOME_BEACH;
    } else {
        double temp = getTemperature(x, y);
        double humid = getMoisture(x, y);
        if (temp > 0.48 && humid < 0.42 && c < 0.75) outBiome = BIOME_DESERT;
        else {
            double altitudeCooling = std::max(0.0, (c - 0.5) / 0.5) * 0.35;
            temp -= altitudeCooling;
            if (temp < 0.3) outBiome = BIOME_TUNDRA;
            else if (humid > 0.5) outBiome = BIOME_FOREST;
            else outBiome = BIOME_PLAINS;
        }
    }

    // Compute height using shared continental value
    double detail = (octaveNoise(nx * 0.35, ny * 0.35, 3, 0.45, 2.0) + 1.0) * 0.5;
    double erosion = (octaveNoise(nx * 0.1, ny * 0.1, 3, 0.4, 2.0) + 1.0) * 0.5;
    int seaLevel = maxHeight / 2;
    double roughness = erosion * 0.6 + 0.3;
    double height;

    if (c < 0.35) {
        double depth = c / 0.35;
        height = seaLevel * 0.4 + depth * seaLevel * 0.5 + detail * 3.0 - 1.5;
    } else if (c < 0.45) {
        double t = (c - 0.35) / 0.1;
        double st = t * t * (3.0 - 2.0 * t);
        height = seaLevel * 0.9 + st * (seaLevel + 2.0 - seaLevel * 0.9) + detail * 2.0 * st;
    } else {
        double inland = (c - 0.45) / 0.55;
        double ist = inland * inland * (3.0 - 2.0 * inland);
        double base = seaLevel + 2.0 + ist * 30.0;
        double amp = 0.3 + ist * 2.0;
        double ridged = 0.0, ridgeAmp = 1.0, ridgeFreq = 0.4, ridgeMax = 0.0;
        for (int i = 0; i < 3; i++) {
            double n = perlinNoise.noise(nx * ridgeFreq, ny * ridgeFreq);
            ridged += (1.0 - std::abs(n)) * ridgeAmp;
            ridgeMax += ridgeAmp;
            ridgeAmp *= 0.5; ridgeFreq *= 2.0;
        }
        ridged /= ridgeMax;
        double shape = detail * (1.0 - ist) + ridged * ist;
        height = base + shape * amp * 30.0 * roughness;
    }

    return std::clamp(static_cast<int>(height), minHeight, maxHeight);
}

double TerrainGenerator::getNoise(int x, int y) {
    double noise = perlinNoise.noise(x * scale, y * scale) + 1.0 / 2.0;
    return noise;
}

double TerrainGenerator::getNoise(int x, int y, int z) {
    double noise = perlinNoise.noise(x * scale, y * scale, z * scale) + 1.0 / 2.0;
    return noise;
}
