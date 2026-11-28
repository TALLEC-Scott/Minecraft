#!/bin/bash
# Build the terrain debugging tools
set -e
cd "$(dirname "$0")/.."

echo "Building mapgen..."
# zlib comes from the system (zlib1g-dev); a Nix g++ doesn't search /usr by
# itself, so add it last (-idirafter) to keep the toolchain's own headers first.
g++ -std=c++20 -O2 -I Libraries/include -I include -idirafter /usr/include scripts/mapgen.cpp src/TerrainGenerator.cpp src/perlin_noise.cpp src/biome.cpp src/tree_shapes.cpp src/terrain_fill.cpp src/cube.cpp src/biomes/*.cpp -L/usr/lib/x86_64-linux-gnu -lz -o scripts/mapgen

echo "Building check_biomes..."
g++ -std=c++20 -O2 -I Libraries/include -I include scripts/check_biomes.cpp src/TerrainGenerator.cpp src/perlin_noise.cpp src/biome.cpp src/tree_shapes.cpp src/terrain_fill.cpp src/cube.cpp src/biomes/*.cpp -o scripts/check_biomes

echo "Building item_wiki..."
g++ -std=c++20 -O2 -I Libraries/include -I include scripts/item_wiki.cpp -o scripts/item_wiki

echo "Done."
