#include "TerrainGenerator.h"
#include "cube.h"
#include <cstdio>

int main() {
    TerrainGenerator terrain(0, 0.1, 0, 128);
    int counts[BIOME_COUNT] = {};
    double tempMin = 1, tempMax = 0, humidMin = 1, humidMax = 0;
    
    for (int x = -2048; x < 2048; x += 4) {
        for (int z = -2048; z < 2048; z += 4) {
            Biome b = terrain.getBiome(x, z);
            counts[b]++;
            double t = terrain.getTemperature(x, z);
            double h = terrain.getMoisture(x, z);
            if (t < tempMin) tempMin = t;
            if (t > tempMax) tempMax = t;
            if (h < humidMin) humidMin = h;
            if (h > humidMax) humidMax = h;
        }
    }
    
    const char* names[] = {"Ocean","Beach","Plains","Forest","Desert","Tundra"};
    int total = 0;
    for (int i = 0; i < BIOME_COUNT; i++) total += counts[i];
    printf("Biome distribution (4096x4096 area):\n");
    for (int i = 0; i < BIOME_COUNT; i++)
        printf("  %-12s %6d  (%5.1f%%)\n", names[i], counts[i], 100.0*counts[i]/total);
    printf("\nTemp range:  %.3f - %.3f\n", tempMin, tempMax);
    printf("Humid range: %.3f - %.3f\n", humidMin, humidMax);
    return 0;
}
