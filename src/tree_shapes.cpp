#include "tree_shapes.h"

#include <algorithm>
#include <cstdlib>

using biomegen::TreeSite;

namespace {

// Ring of leaves at height y, radius r, corners trimmed when r > 1.
void leafRing(const TreeSite& s, int y, int r, bool skipCenter, block_type leaves) {
    for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 1) continue;
            if (dx == 0 && dz == 0 && skipCenter) continue;
            s.leaf(dx, y, dz, leaves);
        }
}

// Oak and birch share the blob canopy: `layers` rings from the trunk top,
// narrowing every second layer, and a small plus on top.
bool blobTree(const TreeSite& s, int trunkH, int radius, block_type log, block_type leaves) {
    const int layers = 3 + static_cast<int>((s.bits >> 16) % 3); // 3..5
    if (!s.fits(s.surface + trunkH + layers + 2)) return false;
    s.claim(radius + 1);
    for (int y = s.surface + 1; y <= s.surface + trunkH; y++) s.log(0, y, 0, log);
    const int top = s.surface + trunkH;
    for (int ly = 0; ly < layers; ly++) leafRing(s, top + ly, std::max(1, radius - ly / 2), ly == 0, leaves);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            if (std::abs(dx) + std::abs(dz) <= 1) s.leaf(dx, top + layers, dz, leaves);
    return true;
}

} // namespace

namespace trees {

bool oak(const TreeSite& s, block_type log, block_type leaves) {
    const int trunkH = 4 + static_cast<int>((s.bits >> 8) % 4);  // 4..7
    const int radius = 2 + static_cast<int>((s.bits >> 12) % 2); // 2..3
    return blobTree(s, trunkH, radius, log, leaves);
}

bool birch(const TreeSite& s) {
    const int trunkH = 5 + static_cast<int>((s.bits >> 8) % 3); // 5..7
    return blobTree(s, trunkH, 2, BIRCH_LOG, BIRCH_LEAVES);
}

bool spruce(const TreeSite& s) {
    const int trunkH = 6 + static_cast<int>((s.bits >> 8) % 4);  // 6..9
    const int radius = 2 + static_cast<int>((s.bits >> 12) % 2); // 2..3
    if (!s.fits(s.surface + trunkH + 2)) return false;
    s.claim(radius + 1);
    for (int y = s.surface + 1; y <= s.surface + trunkH; y++) s.log(0, y, 0, SPRUCE_LOG);
    // Cone from the trunk top down to 3 above the ground; the radius widens
    // downward with alternate rows pulled in, the stepped silhouette of a
    // spruce. One leaf caps the trunk.
    const int top = s.surface + trunkH;
    s.leaf(0, top + 1, 0, SPRUCE_LEAVES);
    for (int d = 0; top - d >= s.surface + 3; d++) {
        int r = 1 + d / 2 - ((d % 2 == 1 && d >= 3) ? 1 : 0);
        leafRing(s, top - d, std::min(r, radius), true, SPRUCE_LEAVES);
    }
    return true;
}

} // namespace trees
