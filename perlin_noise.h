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
    PerlinNoise(unsigned int seed = 123456789);

    double noise(double x, double y);

private:
    double fade(double t);
    double lerp(double t, double a, double b);
    double grad(int hash, double x, double y);

    std::vector<int> p;
};

#endif // PERLIN_NOISE_H
