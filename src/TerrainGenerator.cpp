//
// Created by scott on 03/07/23.
//
// Minecraft 1.18-style terrain shaping: three low-frequency climate
// noises (continentalness, erosion, weirdness) are mapped through
// splines to a terrain height, instead of noise driving height
// directly. See https://www.alanzucconi.com/2022/06/05/minecraft-world-generation/
//
//   height = seaLevel + offset(C) + inlandness · PV · amp(E) + detail
//
// - offset(C):  base elevation by distance from coast (ocean floor
//               plateau, sharp coastline, inland plains, highlands).
// - amp(E):     mountain amplitude by erosion — heavily eroded land is
//               flat no matter how far inland it is.
// - PV:         "peaks & valleys", weirdness folded through
//               1 - |3|W| - 2|. The fold creates ridgelines where the
//               noise crosses its extremes; negative values carve
//               valleys below the base offset.
// - inlandness: 0 at sea, ramps to 1 inland, so mountains never grow
//               out of the ocean or the beach.
#include "TerrainGenerator.h"
#include <algorithm>
#include <cmath>

const BiomeParams TerrainGenerator::BIOME_TABLE[BIOME_COUNT] = {
    // surface       subsurface   baseH  amp   treeDens  treeChance
    {SAND, SAND, 0.0f, 0.0f, 0.0f, 0.0f},    // OCEAN (height handled specially)
    {SAND, SAND, 0.0f, 0.0f, 0.0f, 0.0f},    // BEACH (height handled specially)
    {GRASS, DIRT, 0.0f, 0.0f, 0.15f, 20.0f}, // PLAINS
    {GRASS, DIRT, 0.0f, 0.0f, 0.8f, 50.0f},  // FOREST
    {SAND, SAND, 0.0f, 0.0f, 0.0f, 0.0f},    // DESERT
    {SNOW, DIRT, 0.0f, 0.0f, 0.0f, 0.0f},    // TUNDRA
};

namespace {

struct SplinePt {
    double x, y;
};

// Piecewise interpolation through control points with smoothstep easing
// between neighbors. Smoothstep (rather than plain lerp) gives C1-ish
// continuity at the knots without cubic overshoot — outputs never leave
// the [min, max] of the surrounding control values.
template <int N> double splineEval(const SplinePt (&pts)[N], double x) {
    if (x <= pts[0].x) return pts[0].y;
    if (x >= pts[N - 1].x) return pts[N - 1].y;
    int i = 0;
    while (x > pts[i + 1].x) i++;
    double t = (x - pts[i].x) / (pts[i + 1].x - pts[i].x);
    t = t * t * (3.0 - 2.0 * t);
    return pts[i].y + t * (pts[i + 1].y - pts[i].y);
}

// Base elevation (blocks relative to sea level) by continentalness.
// The steep -0.19 → -0.11 segment is the coastline cliff; the long
// flat middle keeps most land near sea level like Minecraft's plains.
constexpr SplinePt OFFSET_SPLINE[] = {
    {-1.00, -30.0}, // deep ocean floor
    {-0.45, -16.0}, // ocean
    {-0.19, -7.0},  // shallows
    {-0.11, 1.0},   // coastline
    {0.03, 3.0},    // coastal plains
    {0.30, 9.0},    // inland
    {0.65, 16.0},   // far inland plateau
    {1.00, 22.0},   // highlands
};

// Mountain amplitude (blocks) by erosion. Low erosion → dramatic
// relief; high erosion → planed-flat terrain even far inland.
constexpr SplinePt AMP_SPLINE[] = {
    {-1.00, 34.0}, // jagged peaks
    {-0.60, 22.0},
    {-0.20, 12.0},
    {0.20, 6.0},
    {0.60, 3.0},
    {1.00, 1.5}, // near-flat
};

// Normalized FBM rarely leaves ±0.4 (octave averaging compresses the
// range), which would starve the spline endpoints — no deep oceans, no
// jagged-peak erosion, and a PV fold stuck in valley territory. Stretch
// back toward the nominal -1..1 before the splines see the value.
double stretch(double n, double k) { return std::clamp(n * k, -1.0, 1.0); }

// Continentalness thresholds shared by height shaping and biome pick.
constexpr double OCEAN_END = -0.11;  // below: ocean
constexpr double BEACH_END = -0.04;  // below: beach
constexpr double INLAND_FULL = 0.12; // mountains at full strength

} // namespace

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

TerrainGenerator::ColumnSample TerrainGenerator::sampleColumn(int x, int y) {
    double nx = x * scale;
    double ny = y * scale;

    ColumnSample s{};
    // Climate noises. Distinct large offsets decorrelate them despite
    // sharing one permutation table (same trick as temperature/moisture).
    s.continentalness = stretch(octaveNoise(nx * 0.006, ny * 0.006, 4, 0.5, 2.0), 1.7);
    s.erosion = stretch(octaveNoise(nx * 0.015 + 3000.0, ny * 0.015 + 3000.0, 3, 0.5, 2.0), 1.9);
    double weirdness = stretch(octaveNoise(nx * 0.035 + 4000.0, ny * 0.035 + 4000.0, 3, 0.5, 2.0), 2.2);
    // Fold weirdness into peaks & valleys: extremes of W become ridge
    // crests (PV → 1), the middle becomes valley floors (PV → -1).
    s.peaksValleys = 1.0 - std::abs(3.0 * std::abs(weirdness) - 2.0);

    // --- Height ---
    int seaLevel = maxHeight / 2;
    double height = seaLevel + splineEval(OFFSET_SPLINE, s.continentalness);

    // Mountains fade in across the shore band so the beach stays flat
    // and the ocean floor never sprouts ridges.
    double inland = std::clamp((s.continentalness - BEACH_END) / (INLAND_FULL - BEACH_END), 0.0, 1.0);
    inland = inland * inland * (3.0 - 2.0 * inland);
    double amp = splineEval(AMP_SPLINE, s.erosion);
    // Valleys carve at reduced depth so PV < 0 doesn't dig channels
    // below sea level in low plains.
    double pv = s.peaksValleys < 0.0 ? s.peaksValleys * 0.35 : s.peaksValleys;
    height += inland * pv * amp;

    // Small-scale surface detail, everywhere (dunes, ocean-floor ripple).
    height += octaveNoise(nx * 0.35, ny * 0.35, 3, 0.45, 2.0) * 2.0;

    s.height = std::clamp(static_cast<int>(height), minHeight, maxHeight);

    // --- Biome ---
    if (s.continentalness < OCEAN_END) {
        s.biome = BIOME_OCEAN;
    } else if (s.continentalness < BEACH_END) {
        s.biome = BIOME_BEACH;
    } else {
        double temp = getTemperature(x, y);
        double humid = getMoisture(x, y);
        // Desert: hot + dry lowlands. Mountain relief suppresses it so
        // peaks don't read as sand dunes.
        double relief = inland * s.peaksValleys * amp;
        if (temp > 0.48 && humid < 0.42 && relief < 14.0) {
            s.biome = BIOME_DESERT;
        } else {
            // Altitude cooling: high base offset or strong relief → colder.
            double altitude = (s.height - seaLevel) / (double)seaLevel;
            temp -= std::max(0.0, altitude) * 0.7;
            if (temp < 0.3)
                s.biome = BIOME_TUNDRA;
            else if (humid > 0.5)
                s.biome = BIOME_FOREST;
            else
                s.biome = BIOME_PLAINS;
        }
    }
    return s;
}

Biome TerrainGenerator::getBiome(int x, int y) {
    return sampleColumn(x, y).biome;
}

const BiomeParams& TerrainGenerator::getBiomeParams(Biome b) {
    return BIOME_TABLE[b];
}

int TerrainGenerator::getHeight(int x, int y) {
    return sampleColumn(x, y).height;
}

int TerrainGenerator::getHeightAndBiome(int x, int y, Biome& outBiome) {
    ColumnSample s = sampleColumn(x, y);
    outBiome = s.biome;
    return s.height;
}

// Two Minecraft-style cave families, both from 3D Perlin noise:
//
// - "Cheese" caves: one low-frequency 3D noise thresholded high — the
//   rare pockets where it exceeds the cutoff become large open caverns
//   (the holes in the cheese).
// - "Spaghetti" caves: two independent mid-frequency 3D noises. Each
//   noise's zero-surface is a 2D sheet winding through space; where the
//   TWO sheets intersect (both |n| small) you get a 1D winding tube —
//   a long tunnel a few blocks wide.
//
// The vertical frequency is higher than the horizontal so caverns and
// tunnels stretch sideways rather than forming vertical chimneys.
bool TerrainGenerator::isCave(int wx, int wy, int wz) {
    // Spaghetti first: cheaper hit rate to reject on (2 single-octave
    // evals), and tunnels are the most common cave encounter.
    // Low frequency = few, long tunnels. The generous cutoff keeps the
    // bore 4-5 blocks — comfortably walkable — and stops the tunnel from
    // pinching apart where the two noise sheets cross at a shallow angle.
    // Tunnels also fatten slightly with depth, blending into the cavern
    // layer at the bottom.
    double depthWiden = 0.015 * std::clamp(1.0 - wy / 64.0, 0.0, 1.0);
    double tube = 0.085 + depthWiden; // half-thickness of a tunnel
    double s1 = perlinNoise.noise(wx * 0.020 + 700.0, wy * 0.030 + 700.0, wz * 0.020 + 700.0);
    if (std::abs(s1) < tube) {
        double s2 = perlinNoise.noise(wx * 0.020 + 1400.0, wy * 0.030 + 1400.0, wz * 0.020 + 1400.0);
        if (std::abs(s2) < tube) return true;
    }

    // Noodle caves: same intersection trick at ~2× frequency with a
    // thinner cutoff — cramped 1–2 block squeeze-ways that wind tightly.
    // A slow thickness modulator pinches them shut and reopens them so
    // they read as crawl spaces, not endless uniform pipes.
    // Regional: the slow modulator must be positive for noodles to exist
    // at all, so they come in patches (a noodle "system" you stumble
    // into) instead of speckling the whole underground.
    double noodleMod = perlinNoise.noise(wx * 0.01 + 3500.0, wy * 0.01 + 3500.0, wz * 0.01 + 3500.0);
    if (noodleMod > 0.05) {
        double noodleThk = 0.03 + 0.35 * (noodleMod - 0.05) * 0.1;
        double n1 = perlinNoise.noise(wx * 0.055 + 2800.0, wy * 0.075 + 2800.0, wz * 0.055 + 2800.0);
        if (std::abs(n1) < noodleThk) {
            double n2 = perlinNoise.noise(wx * 0.055 + 4200.0, wy * 0.075 + 4200.0, wz * 0.055 + 4200.0);
            if (std::abs(n2) < noodleThk) return true;
        }
    }

    // Cheese caverns: low frequency → large chambers; the second octave
    // roughens their walls. The threshold falls with depth, so near the
    // surface caverns are rare pockets while deep down they open into
    // proper rooms (Minecraft's cheese caves live at the bottom too).
    double c = perlinNoise.noise(wx * 0.009 + 2100.0, wy * 0.016 + 2100.0, wz * 0.009 + 2100.0);
    c += 0.35 * perlinNoise.noise(wx * 0.027 + 2100.0, wy * 0.048 + 2100.0, wz * 0.027 + 2100.0);
    double depth01 = std::clamp(1.0 - wy / 64.0, 0.0, 1.0); // 0 at sea level, 1 at world floor
    // Steep depth bias: near the surface caverns barely exist; the big
    // rooms live in the bottom third of the world.
    return c > 0.66 - 0.38 * depth01;
}

double TerrainGenerator::getNoise(int x, int y) {
    double noise = perlinNoise.noise(x * scale, y * scale) + 1.0 / 2.0;
    return noise;
}

double TerrainGenerator::getNoise(int x, int y, int z) {
    double noise = perlinNoise.noise(x * scale, y * scale, z * scale) + 1.0 / 2.0;
    return noise;
}
