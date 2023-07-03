#include "perlin_noise.h"

PerlinNoise::PerlinNoise() {
    for (int i = 0; i < 256; ++i) {
        p[i] = i;
    }

    // Shuffle the array
    for (int i = 0; i < 256; ++i) {
        int j = rand() & 255;
        std::swap(p[i], p[j]);
    }

    // Duplicate the permutation array
    for (int i = 0; i < 256; ++i) {
        p[i + 256] = p[i];
    }
}

double PerlinNoise::noise(double x, double y) {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    double u = fade(x);
    double v = fade(y);

    int A = p[X] + Y;
    int B = p[X + 1] + Y;

    double res = lerp(v, lerp(u, grad(p[A], x, y), grad(p[B], x - 1, y)), lerp(u, grad(p[A + 1], x, y - 1), grad(p[B + 1], x - 1, y - 1)));

    return (res + 1.0) / 2.0; // Normalize to [0, 1]
}

double PerlinNoise::fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double PerlinNoise::lerp(double t, double a, double b) {
    return a + t * (b - a);
}

double PerlinNoise::grad(int hash, double x, double y) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}