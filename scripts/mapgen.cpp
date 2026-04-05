// Standalone tool: generates a top-down PNG map of the terrain
// Build: g++ -std=c++17 -O2 -I Libraries/include mapgen.cpp TerrainGenerator.cpp perlin_noise.cpp -o mapgen
// Usage: ./mapgen [size] [output.png]

#include "TerrainGenerator.h"
#include "cube.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

// Minimal PNG writer (uncompressed)
#include <zlib.h>

static void writePNG(const char* filename, const unsigned char* pixels, int w, int h) {
    FILE* f = fopen(filename, "wb");
    if (!f) { fprintf(stderr, "Can't open %s\n", filename); return; }

    auto chunk = [&](const char* type, const unsigned char* data, int len) {
        unsigned char buf[4];
        buf[0]=len>>24; buf[1]=len>>16; buf[2]=len>>8; buf[3]=len;
        fwrite(buf, 1, 4, f);
        fwrite(type, 1, 4, f);
        if (len) fwrite(data, 1, len, f);
        unsigned long crc = crc32(0, (const unsigned char*)type, 4);
        if (len) crc = crc32(crc, data, len);
        buf[0]=crc>>24; buf[1]=crc>>16; buf[2]=crc>>8; buf[3]=crc;
        fwrite(buf, 1, 4, f);
    };

    unsigned char sig[] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13];
    ihdr[0]=w>>24; ihdr[1]=w>>16; ihdr[2]=w>>8; ihdr[3]=w;
    ihdr[4]=h>>24; ihdr[5]=h>>16; ihdr[6]=h>>8; ihdr[7]=h;
    ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; // 8-bit RGB
    chunk("IHDR", ihdr, 13);

    // Raw image data with filter bytes
    std::vector<unsigned char> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0); // filter: none
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 3;
            raw.push_back(pixels[idx]);
            raw.push_back(pixels[idx+1]);
            raw.push_back(pixels[idx+2]);
        }
    }

    uLongf compLen = compressBound(raw.size());
    std::vector<unsigned char> comp(compLen);
    compress(comp.data(), &compLen, raw.data(), raw.size());
    chunk("IDAT", comp.data(), compLen);
    chunk("IEND", nullptr, 0);
    fclose(f);
    printf("Wrote %s (%dx%d)\n", filename, w, h);
}

int main(int argc, char* argv[]) {
    int mapSize = 512;
    const char* output = "terrain_map.png";
    if (argc > 1) mapSize = atoi(argv[1]);
    if (argc > 2) output = argv[2];

    // Same parameters as World::World()
    TerrainGenerator terrain(0, 0.1, 0, CHUNK_HEIGHT);
    int waterLevel = CHUNK_HEIGHT / 2;

    std::vector<unsigned char> pixels(mapSize * mapSize * 3);

    // Find height range for color mapping
    int minH = 9999, maxH = -9999;
    std::vector<int> heights(mapSize * mapSize);
    for (int y = 0; y < mapSize; y++) {
        for (int x = 0; x < mapSize; x++) {
            // Center map on origin, 1 block per pixel
            int wx = x - mapSize / 2;
            int wy = y - mapSize / 2;
            int h = terrain.getHeight(wx, wy);
            heights[y * mapSize + x] = h;
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }

    printf("Height range: %d to %d (sea level: %d)\n", minH, maxH, waterLevel);

    // Biome color palette: [r, g, b]
    static const unsigned char BIOME_COLORS[BIOME_COUNT][3] = {
        {20,  50, 180},   // OCEAN - deep blue
        {220, 200, 140},  // BEACH - sandy yellow
        {100, 180,  60},  // PLAINS - light green
        {30,  100,  20},  // FOREST - dark green
        {210, 190, 100},  // DESERT - tan
        {200, 220, 230},  // TUNDRA - pale blue-white
    };

    for (int y = 0; y < mapSize; y++) {
        for (int x = 0; x < mapSize; x++) {
            int wx = x - mapSize / 2;
            int wy = y - mapSize / 2;
            int h = heights[y * mapSize + x];
            Biome biome = terrain.getBiome(wx, wy);

            unsigned char r = BIOME_COLORS[biome][0];
            unsigned char g = BIOME_COLORS[biome][1];
            unsigned char b = BIOME_COLORS[biome][2];

            // Height shading within biome
            float shade = 0.7f + 0.3f * (float)(h - minH) / (float)(maxH - minH + 1);
            r = (unsigned char)(r * shade);
            g = (unsigned char)(g * shade);
            b = (unsigned char)(b * shade);

            // Water depth darkening
            if (biome == BIOME_OCEAN && h <= waterLevel) {
                float depth = (float)(waterLevel - h) / 20.0f;
                r = (unsigned char)(r * (1.0f - depth * 0.5f));
                g = (unsigned char)(g * (1.0f - depth * 0.5f));
                b = (unsigned char)(b * (1.0f - depth * 0.3f));
            }

            int idx = (y * mapSize + x) * 3;
            pixels[idx] = r;
            pixels[idx+1] = g;
            pixels[idx+2] = b;
        }
    }

    writePNG(output, pixels.data(), mapSize, mapSize);
    return 0;
}
