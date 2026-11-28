#pragma once

// Stock tree shapes any biome can grow (biome.h TreeSite). A biome that
// needs another shape writes it in its own src/biomes/<name>.cpp with the
// same TreeSite calls; keep canopies within biomegen::TREE_MAX_RADIUS.

#include "biome.h"

namespace trees {

// Round canopy on a 4..7 tall trunk (Minecraft's small oak).
bool oak(const biomegen::TreeSite& s, block_type log = WOOD, block_type leaves = LEAVES);
// Taller, narrower: 5..7 trunk, radius 2.
bool birch(const biomegen::TreeSite& s);
// Stepped cone on a 6..9 trunk.
bool spruce(const biomegen::TreeSite& s);

} // namespace trees
