#pragma once

#include <iostream>
#include <cmath>

class PerlinNoise {
public:
    PerlinNoise();
    double noise(double x, double y);

private:
    double fade(double t);
    double lerp(double t, double a, double b);
    double grad(int hash, double x, double y);

    int p[512];
};