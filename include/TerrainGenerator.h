#pragma once
#include "cube.h"
#include "perlin_noise.h"

// Minecraft 1.18+-inspired terrain. Five climate noises (temperature,
// humidity, continentalness, erosion, weirdness) drive biome selection and
// a spline that gives baseHeight + verticalFactor. A 3D density noise then
// carves overhangs and shares its scalar field with the cave carver — no
// separate "2D heightmap then caves" pass. Scaled to the 128-tall world
// (Minecraft's -64..320 range shrinks ~3×).

struct Climate {
    double temperature;      // -1..1
    double humidity;         // -1..1
    double continentalness;  // -1..1 (< -0.4 ocean, > 0.3 deep inland)
    double erosion;          // -1..1 (high = flatter terrain)
    double weirdness;        // -1..1
    double pv;               // peaks-and-valleys reshape of weirdness, -1..1
};

struct BiomeParams {
    block_type surfaceBlock;
    block_type subsurfaceBlock;
    block_type underwaterSurface;  // below sea-level top layer (SAND/GRAVEL)
    float treeDensity;             // 0..1 probability weight
    float treeChance;              // 0..100
    bool hasSnow;                  // paint SNOW on top at high altitude
};

class TerrainGenerator {
  public:
    TerrainGenerator(unsigned int seed, float scale, int minHeight, int maxHeight);

    // Scans the column top-down until the 3D density turns solid. O(height).
    int getHeight(int x, int y);
    Biome getBiome(int x, int y);
    int getHeightAndBiome(int x, int y, Biome& outBiome);

    // Climate samplers — values normalised to [0, 1] (legacy API shape).
    double getTemperature(int x, int y);
    double getMoisture(int x, int y);
    const BiomeParams& getBiomeParams(Biome b);
    double getNoise(int x, int y);
    double getNoise(int x, int y, int z);

    // --- New API used by generateChunkData ---
    Climate sampleClimate(int x, int z);
    // Target terrain height for this (x,z), mapped into [0, 128]. Pure spline
    // of continentalness → erosion-flatten → PV-push. No noise added here;
    // that comes in via `density`.
    double splineBaseHeight(const Climate& c);
    // Vertical "stretch" [0.2, 4.0] — oceans get a small factor (flat floor),
    // windswept hills get a big factor (dramatic ups/downs).
    double splineHeightFactor(const Climate& c);
    // Main density field: (baseH - y) * factor + 3D noise. > 0 = solid.
    double density(int x, int y, int z, double baseH, double factor);
    // Cheese + spaghetti carver. True = carve to air.
    bool inCave(int x, int y, int z);
    // Coal-ore selector: returns true where stone should become COAL_ORE.
    bool oreHit(int x, int y, int z);
    Biome pickBiome(const Climate& c, double heightAboveSea);

    static constexpr int SEA_LEVEL = 64;

  private:
    double octaveNoise(double x, double y, int octaves, double persistence, double lacunarity);
    double octaveNoise3(double x, double y, double z, int octaves, double persistence, double lacunarity);
    // Same as octaveNoise but driven by an arbitrary PerlinNoise instance.
    // Lets each climate parameter sum its own octaves without cross-
    // contaminating neighbors via a shared permutation table.
    double octaveNoiseFrom(PerlinNoise& p, double x, double y, int octaves, double persistence, double lacunarity);
    // Ridged noise: 1 - |noise|, with octaves. Produces sharp mountain ridges
    // where the base would otherwise be rolling hills.
    double ridgedNoiseFrom(PerlinNoise& p, double x, double y, int octaves, double persistence, double lacunarity);

    // One PerlinNoise per parameter so they're statistically independent.
    // Each permutation table is ~2 KB — 10 instances is still trivial.
    PerlinNoise perlinNoise;      // legacy getNoise wrapper; also feeds ore noise
    PerlinNoise noiseTemp;
    PerlinNoise noiseHumid;
    PerlinNoise noiseCont;
    PerlinNoise noiseErosion;
    PerlinNoise noiseWeird;
    PerlinNoise noise3DA;
    PerlinNoise noise3DB;
    PerlinNoise noiseCaveCheese;
    PerlinNoise noiseCaveSpaghetti;

    float scale;
    int maxHeight;  // minHeight was always 0 (the bedrock layer) so it's implicit.

    static const BiomeParams BIOME_TABLE[BIOME_COUNT];
};
