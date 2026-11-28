#pragma once

// Forest internals (src/biomes/forest.cpp), exposed for its tests.

#include <cstdint>

#include "biome.h"

namespace forest {

inline constexpr int GROVE_CELL = 24;             // one candidate birch grove per GROVE_CELL² blocks
inline constexpr uint64_t GROVE_CHANCE = 40;      // % of grove cells that hold one
inline constexpr uint64_t FALLEN_LOG_CHANCE = 4;  // % of open feature cells with a fallen log

// True if the world column lies in a birch grove (deterministic per seed).
bool inBirchGrove(uint64_t seed, int wx, int wz);

bool fancyOak(const biomegen::TreeSite& s);
bool growTree(const biomegen::TreeSite& s);
void decorate(const biomegen::FeatureSite& f);
block_type groundCover(const biomegen::GroundSite& g);

} // namespace forest
