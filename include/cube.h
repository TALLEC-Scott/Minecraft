#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// boost::hash_combine — distributes small-coordinate keys much better
// than xor-shift. Used by Vec2Hash, IVec3Hash, and anywhere else we
// need to hash a structured key.
inline void hashCombine(std::size_t& seed, int v) {
    std::hash<int> h;
    seed ^= h(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// 6-connected neighbor offsets (±x, ±y, ±z). Shared by water simulation,
// light BFS, and anywhere else that walks face neighbors.
inline constexpr int DIRS_6[6][3] = {
    { 1, 0, 0}, {-1, 0, 0},
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0, 1}, { 0, 0,-1},
};

#define RENDER_DISTANCE 16
#define CHUNK_SIZE 16
#define CHUNK_HEIGHT 128

// Runtime greedy meshing toggle (default off due to AO interpolation artifacts)
inline bool g_greedyMeshing = false;
inline bool g_fancyLeaves = true;

enum block_type : uint8_t {
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

// Water level encoding: bits 0-2 hold the flow level (0-7), bit 7 is the
// "falling" flag. Level 0 without the flag is a source (infinite). Level 0
// WITH the flag is a falling-water column cell — it needs water above to
// survive and decays immediately if that chain breaks.
static constexpr uint8_t WATER_LEVEL_MASK = 0x07;
static constexpr uint8_t WATER_FALLING_FLAG = 0x80;
static constexpr uint8_t WATER_MAX_FLOW = 7; // cells at level 7 don't spread further

// Flow direction encoding packed into the AO byte's upper bits at mesh
// build time. The AO value uses bits 0-1 (0-3); we store a "has
// directional flow" flag in bit 2 and a compass angle index (0-15) in
// bits 3-6. Source blocks and falling water leave these bits zero
// (gentle omnidirectional ripple in the shader).
static constexpr uint8_t FLOW_HAS_DIR_BIT  = 0x04; // bit 2
static constexpr uint8_t FLOW_ANGLE_SHIFT  = 3;     // bits 3-6
static constexpr uint8_t FLOW_ANGLE_MASK   = 0x0F;  // 4 bits → 16 directions
inline uint8_t waterFlowLevel(uint8_t raw) { return raw & WATER_LEVEL_MASK; }
inline bool waterIsFalling(uint8_t raw) { return (raw & WATER_FALLING_FLAG) != 0; }
inline bool waterIsSource(uint8_t raw) { return (raw & (WATER_LEVEL_MASK | WATER_FALLING_FLAG)) == 0; }

enum BlockFlag : uint32_t {
    BF_NONE = 0,
    BF_SOLID = 1u << 0,       // blocks movement / collision
    BF_OPAQUE = 1u << 1,      // fully blocks light
    BF_TRANSPARENT = 1u << 2, // can see through (skip face between same type)
    BF_TRANSLUCENT = 1u << 3,  // partially see-through: reduces sky light, don't cull faces (leaves)
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
        /* LEAVES    */ BF_SOLID | BF_TRANSLUCENT,
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

inline uint8_t getBlockLightEmission(block_type t) {
    if (t == GLOWSTONE) return 15;
    return 0;
}

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
