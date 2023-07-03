#ifndef PERLIN_NOISE_H
#define PERLIN_NOISE_H
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <iostream>
#include <vector>

class PerlinNoise {
public:
    PerlinNoise(unsigned int seed);

    double noise(double x, double y); // 2D noise
    double noise(double x, double y, double z); // 3D noise

private:
    std::vector<int> p;
    double fade(double t);
    double lerp(double t, double a, double b);
    double grad(int hash, double x, double y);
    double grad(int hash, double x, double y, double z);
};
#endif // PERLIN_NOISE_H
