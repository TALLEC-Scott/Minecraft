#pragma once

#include <cstdint>

#define RENDER_DISTANCE 16
#define CHUNK_SIZE 16
#define CHUNK_HEIGHT 128

// Runtime greedy meshing toggle (default off due to AO interpolation artifacts)
inline bool g_greedyMeshing = false;

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
    GRAVEL,
    CACTUS
};

enum Biome { BIOME_OCEAN, BIOME_BEACH, BIOME_PLAINS, BIOME_FOREST, BIOME_DESERT, BIOME_TUNDRA, BIOME_COUNT };

enum BlockFlag : uint32_t {
    BF_NONE = 0,
    BF_SOLID = 1u << 0,       // blocks movement / collision
    BF_OPAQUE = 1u << 1,      // fully blocks light
    BF_TRANSPARENT = 1u << 2, // can see through (skip face between same type)
    BF_FILTERING = 1u << 3,   // reduces sky light by 1 per block (like leaves)
    BF_LIQUID = 1u << 4,      // renders with blending, no collision
};

inline uint32_t getBlockFlags(block_type t) {
    // clang-format off
    static const uint32_t flags[] = {
        /* AIR       */ BF_TRANSPARENT,
        /* GRASS     */ BF_SOLID | BF_OPAQUE,
        /* DIRT      */ BF_SOLID | BF_OPAQUE,
        /* STONE     */ BF_SOLID | BF_OPAQUE,
        /* COAL_ORE  */ BF_SOLID | BF_OPAQUE,
        /* BEDROCK   */ BF_SOLID | BF_OPAQUE,
        /* WATER     */ BF_TRANSPARENT | BF_LIQUID,
        /* SAND      */ BF_SOLID | BF_OPAQUE,
        /* GLOWSTONE */ BF_SOLID | BF_OPAQUE,
        /* WOOD      */ BF_SOLID | BF_OPAQUE,
        /* LEAVES    */ BF_SOLID | BF_FILTERING,
        /* SNOW      */ BF_SOLID | BF_OPAQUE,
        /* GRAVEL    */ BF_SOLID | BF_OPAQUE,
        /* CACTUS    */ BF_SOLID | BF_OPAQUE,
    };
    // clang-format on
    int idx = static_cast<int>(t);
    if (idx < 0 || idx >= static_cast<int>(sizeof(flags) / sizeof(flags[0]))) return BF_NONE;
    return flags[idx];
}

inline bool hasFlag(block_type t, uint32_t f) {
    return (getBlockFlags(t) & f) != 0;
}

enum StepSound {
    STEP_NONE,
    STEP_GRASS,
    STEP_STONE,
    STEP_SAND,
    STEP_GRAVEL,
    STEP_SNOW,
    STEP_WOOD,
    STEP_WATER,
    STEP_CLOTH
};

inline StepSound getStepSound(block_type t) {
    // clang-format off
    static const StepSound sounds[] = {
        /* AIR       */ STEP_NONE,
        /* GRASS     */ STEP_GRASS,
        /* DIRT      */ STEP_GRASS,
        /* STONE     */ STEP_STONE,
        /* COAL_ORE  */ STEP_STONE,
        /* BEDROCK   */ STEP_STONE,
        /* WATER     */ STEP_WATER,
        /* SAND      */ STEP_SAND,
        /* GLOWSTONE */ STEP_STONE,
        /* WOOD      */ STEP_WOOD,
        /* LEAVES    */ STEP_GRASS,
        /* SNOW      */ STEP_SNOW,
        /* GRAVEL    */ STEP_GRAVEL,
        /* CACTUS    */ STEP_CLOTH,
    };
    // clang-format on
    int idx = static_cast<int>(t);
    if (idx < 0 || idx >= static_cast<int>(sizeof(sounds) / sizeof(sounds[0]))) return STEP_NONE;
    return sounds[idx];
}

// Convert world block coordinate to chunk coordinate (handles negative correctly)
inline int worldToChunk(int coord) {
    return (coord >= 0) ? coord / CHUNK_SIZE : (coord - CHUNK_SIZE + 1) / CHUNK_SIZE;
}

// Convert world block coordinate to local coordinate within chunk
inline int worldToLocal(int coord, int chunkCoord) {
    return coord - chunkCoord * CHUNK_SIZE;
}

class Cube {
  public:
    Cube() : type(AIR) {}

    block_type getType() const { return type; }
    void setType(block_type t) { type = t; }

  private:
    block_type type;
};
