#pragma once

// Plains generation helpers (src/biomes/plains.cpp), exposed for tests.

#include <cstdint>

#include "biome.h"

namespace plains {

inline constexpr int GRASS_CELL = 9;               // tall-grass drift noise cell (blocks)
inline constexpr int MEADOW_CELL = 14;             // one possible flower patch per cell
inline constexpr uint64_t MEADOW_CHANCE = 45;      // % of cells with a patch
inline constexpr int COPSE_CELL = 48;              // one possible copse per cell
inline constexpr uint64_t COPSE_CHANCE = 18;       // % of cells with a copse
inline constexpr int COPSE_RADIUS = 4;             // trunks within this of its centre
inline constexpr uint64_t LONE_TREE_PER_MILLE = 8; // per tree cell outside copses

// 0..1 smooth noise: how thick the tall grass is around (wx, wz).
double grassDensity(int wx, int wz, uint64_t seed);
// The flower patch covering (wx, wz) (AIR if none) and how deep inside it
// the column is, 1 at the centre .. 0 at the rim.
block_type meadowAt(int wx, int wz, uint64_t seed, double& strength);

block_type groundCover(const biomegen::GroundSite& g);
bool growTree(const biomegen::TreeSite& s);

} // namespace plains
