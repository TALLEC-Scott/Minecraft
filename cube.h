#pragma once

#define RENDER_DISTANCE 8
#define CHUNK_SIZE 16
#define CHUNK_HEIGHT 64

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
    LEAVES
};

class Cube {
public:
    Cube() : type(AIR) {}

    block_type getType() const { return type; }
    void setType(block_type t) { type = t; }

private:
    block_type type;
};
