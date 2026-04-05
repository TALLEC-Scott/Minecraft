#pragma once

#define RENDER_DISTANCE 16
#define CHUNK_SIZE 16
#define CHUNK_HEIGHT 128

enum block_type {
    AIR,
    GRASS,
    DIRT,
    STONE,
    COAL_ORE,
    BEDROCK,
    WATER,
    SAND,
    GLOWSTONE,
    WOOD,
    LEAVES,
    SNOW,
    GRAVEL
};

enum Biome {
    BIOME_OCEAN,
    BIOME_BEACH,
    BIOME_PLAINS,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_TUNDRA,
    BIOME_COUNT
};

class Cube {
public:
    Cube() : type(AIR) {}

    block_type getType() const { return type; }
    void setType(block_type t) { type = t; }

private:
    block_type type;
};
