// Savanna: warm, dry, open grassland (Minecraft's savanna). Golden grass
// thick with tall grass in drifting patches, and scattered flat-topped
// acacias, now and then gathered in small groves, with the odd oak.
//
// Acacia (after Minecraft's): a trunk that leans diagonally (one block
// sideways per block up) near its top and ends in a wide, thin canopy, a
// rounded slab of radius 3..4 under a smaller cap; often a second branch
// forks off the trunk lower down, leaning the other way, with its own
// smaller canopy. Everything stays within biomegen::TREE_MAX_RADIUS of the
// trunk column.

#include "biome_defs.h"
#include "tree_shapes.h"

#include <algorithm>
#include <cstdlib>

namespace {

uint64_t mix(uint64_t v) {
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

uint64_t cellHash(uint64_t seed, int cx, int cz, uint64_t salt) {
    return mix(seed ^ mix((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32 | static_cast<uint32_t>(cz)) ^
                          (salt * 0xD1B54A32D192ED03ULL)));
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Smooth 0..1 value noise over `cell`-block cells: bilinear between hashed
// corner values, so patches fade instead of ending on a grid line.
float patchNoise(uint64_t seed, int wx, int wz, int cell, uint64_t salt) {
    const int cx = floorDiv(wx, cell), cz = floorDiv(wz, cell);
    const float fx = static_cast<float>(wx - cx * cell) / static_cast<float>(cell);
    const float fz = static_cast<float>(wz - cz * cell) / static_cast<float>(cell);
    auto corner = [&](int dx, int dz) {
        return static_cast<float>(cellHash(seed, cx + dx, cz + dz, salt) >> 40) / static_cast<float>(1ULL << 24);
    };
    const float sx = fx * fx * (3.0f - 2.0f * fx), sz = fz * fz * (3.0f - 2.0f * fz);
    const float top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * sx;
    const float bot = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * sx;
    return top + (bot - top) * sz;
}

constexpr uint64_t SALT_GROVE = 0x5A7A, SALT_GRASS = 0x6A55;

// Rounded flat slab of leaves: every column within `r` (+0.5) of the
// centre, so radius 3 is Minecraft's 7×7 minus its corners.
void slab(const biomegen::TreeSite& s, int cx, int y, int cz, int r) {
    const int lim = r * r + r; // (r + 0.5)² rounded down
    for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++)
            if (dx * dx + dz * dz <= lim) s.leaf(cx + dx, y, cz + dz, ACACIA_LEAVES);
}

// Diagonal step for a lean direction 0..3 (+X, -X, +Z, -Z).
constexpr int DIR_X[4] = {1, -1, 0, 0};
constexpr int DIR_Z[4] = {0, 0, 1, -1};

bool acacia(const biomegen::TreeSite& s) {
    const uint64_t b = s.bits;
    const int height = 5 + static_cast<int>(b % 3) + static_cast<int>((b >> 2) % 3); // 5..9 logs
    const int dir = static_cast<int>((b >> 4) % 4);
    // The lean: 1..2 blocks sideways, then 0..2 more logs straight up.
    const int lean = 1 + static_cast<int>((b >> 6) % 2);
    const int leanStart = std::max(2, height - lean - static_cast<int>((b >> 7) % 3));
    // Main canopy: radius 4 only on a short lean, so it stays within reach.
    const int radius = (lean == 1 && (b >> 9) % 3 == 0) ? 4 : 3;
    if (!s.fits(s.surface + height + 2)) return false;

    s.claim(1);
    int x = 0, z = 0, leaned = 0, topY = s.surface;
    for (int h = 0; h < height; h++) {
        if (h >= leanStart && leaned < lean) {
            x += DIR_X[dir];
            z += DIR_Z[dir];
            leaned++;
        }
        topY = s.surface + 1 + h;
        s.log(x, topY, z, ACACIA_LOG);
    }
    // The canopy sits level with the last log (which stays) plus a cap.
    slab(s, x, topY, z, radius);
    slab(s, x, topY + 1, z, radius - 2);
    if (radius == 3)
        for (int d = 0; d < 4; d++) s.leaf(x + 2 * DIR_X[d], topY + 1, z + 2 * DIR_Z[d], ACACIA_LEAVES);

    // Second branch (about 2 trees in 3): leaves the trunk 2..3 blocks
    // under the lean in another direction, 1..3 blocks long, with a
    // radius-2 canopy, often reaching out a little lower than the main one.
    const int dir2 = static_cast<int>((b >> 11) % 4);
    if (dir2 == dir || (b >> 13) % 3 == 0) return true;
    const int len2 = 1 + static_cast<int>((b >> 15) % 3);
    const int start2 = std::max(1, leanStart - 1 - static_cast<int>((b >> 17) % 2));
    int bx = 0, bz = 0, by = s.surface + start2;
    // Its canopy (2 layers) must leave an air gap under the main one, or
    // the two merge into one blob.
    for (int n = 0; n < len2 && by + 1 <= topY - 3; n++) {
        bx += DIR_X[dir2];
        bz += DIR_Z[dir2];
        by++;
        s.log(bx, by, bz, ACACIA_LOG);
    }
    if (bx == 0 && bz == 0) return true;
    slab(s, bx, by, bz, 2);
    slab(s, bx, by + 1, bz, 1);
    return true;
}

// Trees: groves of acacias (about a third of the land, in coarse patches)
// and lone ones between them; one tree in eight is a small oak.
bool growTree(const biomegen::TreeSite& s) {
    const float grove = patchNoise(s.seed, s.wx, s.wz, 40, SALT_GROVE);
    const float keep = grove > 0.62f ? 0.85f : 0.3f;
    if (static_cast<float>((s.bits >> 20) % 1000) >= keep * 1000.0f) return false;
    if ((s.bits >> 32) % 8 == 0) return trees::oak(s);
    return acacia(s);
}

// Tall grass in drifting patches (15..60%), a rare dandelion.
block_type groundCover(const biomegen::GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    const float density = 0.15f + 0.45f * patchNoise(g.seed, g.wx, g.wz, 12, SALT_GRASS);
    if (static_cast<float>(g.roll) < density * 10000.0f) return TALL_GRASS;
    return g.roll >= 9985 ? DANDELION : AIR;
}

} // namespace

const BiomeDef SAVANNA_DEF = {
    "Savanna",
    GRASS, DIRT, SAND,
    0.14f, &growTree,
    nullptr,
    &groundCover,
    // Minecraft's savanna colours over its plains ones: grass #BFB755 /
    // #91BD59, foliage #AEA42A / #77AB2F (desert shares them, so the
    // desert edge needs no fade; plains fades to green over 9 blocks).
    {1.32f, 0.97f, 0.95f}, // grass tint
    {1.46f, 0.96f, 0.89f}, // foliage tint
};
