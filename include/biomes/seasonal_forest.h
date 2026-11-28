#pragma once

// Seasonal forest generation helpers (src/biomes/seasonal_forest.cpp),
// exposed for tests.

#include <cstdint>

#include "biome.h"

namespace seasonal_forest {

// Below this clearing value a column is open meadow: no trees, many flowers.
inline constexpr double CLEARING = 0.36;

// 0..1 smooth patch noise for clearings (low = open meadow).
double clearing(uint64_t seed, int wx, int wz);
// 0..1 smooth patch noise: how far a stand has turned (high = autumn).
double autumn(uint64_t seed, int wx, int wz);

bool growTree(const biomegen::TreeSite& s);
void decorate(const biomegen::FeatureSite& f);
block_type groundCover(const biomegen::GroundSite& g);

} // namespace seasonal_forest
