#!/bin/bash
# Build the terrain debugging tools
set -e
cd "$(dirname "$0")/.."

echo "Building mapgen..."
g++ -std=c++17 -O2 -I Libraries/include -I include scripts/mapgen.cpp src/TerrainGenerator.cpp src/perlin_noise.cpp -lz -o scripts/mapgen

echo "Building check_biomes..."
g++ -std=c++17 -O2 -I Libraries/include -I include scripts/check_biomes.cpp src/TerrainGenerator.cpp src/perlin_noise.cpp -o scripts/check_biomes

echo "Done."
