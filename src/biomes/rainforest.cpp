// Rainforest: the hot, wet corner of the climate table. A layered jungle:
// emergent giants on 2×2 jungle-log trunks with broad flat crowns, branch
// clusters and buttress roots; a middle storey of smaller jungle trees; low
// leafy bushes on the floor; leaf "vines" hanging from every canopy; and a
// floor of tall grass and ferns that still leaves bare grass to walk on.

#include <cstdint>
#include <cstdlib>

#include "biome_defs.h"
#include "tree_shapes.h"

using biomegen::TreeSite;

namespace {

// Stable per-offset randomness inside one tree (vine lengths, ragged
// crown edges), so both chunks of a border draw the same tree.
uint32_t mix(uint64_t bits, int a, int b, int c) {
    uint64_t v = bits ^ (static_cast<uint64_t>(static_cast<uint32_t>(a)) * 0x9E3779B97F4A7C15ULL) ^
                 (static_cast<uint64_t>(static_cast<uint32_t>(b)) * 0xC2B2AE3D27D4EB4FULL) ^
                 (static_cast<uint64_t>(static_cast<uint32_t>(c)) * 0x165667B19E3779F9ULL);
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<uint32_t>(v ^ (v >> 31));
}

// A strand of leaves hanging from (dx, y, dz) downward.
void vine(const TreeSite& s, int dx, int y, int dz, int len) {
    for (int d = 0; d < len; d++) s.leaf(dx, y - d, dz, LEAVES);
}

// Disc of leaves at height y around a centre given in half blocks (cx2, cz2
// = 2 × centre), radius r. Edge cells are dropped at random for a ragged
// crown; `vines` hangs strands from the rim (1 in `vineOdds`).
void crownLayer(const TreeSite& s, int y, int cx2, int cz2, float r, int vineOdds, int maxVine) {
    const int reach = static_cast<int>(r) + 1;
    const float r2 = r * r;
    const float inner2 = (r - 1.0f) * (r - 1.0f);
    const int cx = cx2 / 2, cz = cz2 / 2;
    for (int dx = cx - reach; dx <= cx + reach + 1; dx++)
        for (int dz = cz - reach; dz <= cz + reach + 1; dz++) {
            const float fx = static_cast<float>(2 * dx - cx2) * 0.5f;
            const float fz = static_cast<float>(2 * dz - cz2) * 0.5f;
            const float d2 = fx * fx + fz * fz;
            if (d2 > r2) continue;
            if (std::abs(dx) > biomegen::TREE_MAX_RADIUS || std::abs(dz) > biomegen::TREE_MAX_RADIUS) continue;
            const uint32_t h = mix(s.bits, dx, y, dz);
            const bool rim = d2 > inner2;
            if (rim && h % 4 == 0) continue; // ragged edge
            s.leaf(dx, y, dz, LEAVES);
            if (rim && vineOdds > 0 && (h >> 8) % static_cast<uint32_t>(vineOdds) == 0)
                vine(s, dx, y - 1, dz, 1 + static_cast<int>((h >> 16) % static_cast<uint32_t>(maxVine)));
        }
}

// Emergent tree: 2×2 trunk 13..19 tall, buttress roots, one or two branch
// clusters partway up and a broad flat crown (radius ≤ 4.5 around the
// trunk's centre, so no leaf is more than 5 columns from the (0, 0) log).
bool giantTree(const TreeSite& s) {
    const int trunkH = 13 + static_cast<int>((s.bits >> 8) % 7);
    const int top = s.surface + trunkH;
    if (!s.fits(top + 3)) return false;
    // Claim only around the trunk: ferns and grass grow in the shade, as
    // on a jungle floor.
    s.claim(1);
    // Trunk; starts below the surface so it stays grounded on a slope.
    for (int y = s.surface - 1; y < top; y++)
        for (int dx = 0; dx <= 1; dx++)
            for (int dz = 0; dz <= 1; dz++) s.log(dx, y, dz, JUNGLE_LOG);
    // Buttress roots: a 1..2 high log against a few trunk sides.
    static const int ROOTS[8][2] = {{-1, 0}, {-1, 1}, {2, 0}, {2, 1}, {0, -1}, {1, -1}, {0, 2}, {1, 2}};
    for (int r = 0; r < 8; r++) {
        const uint32_t h = mix(s.bits, ROOTS[r][0], 0, ROOTS[r][1]);
        if (h % 4 != 0) continue;
        const int hgt = 1 + static_cast<int>((h >> 4) % 2);
        for (int y = s.surface - 1; y <= s.surface + hgt; y++) s.log(ROOTS[r][0], y, ROOTS[r][1], JUNGLE_LOG);
    }
    // Branch clusters: a short diagonal limb with a leaf ball at its end.
    static const int DIRS[4][2] = {{-1, -1}, {2, -1}, {-1, 2}, {2, 2}};
    const int branches = 1 + static_cast<int>((s.bits >> 16) % 2);
    for (int b = 0; b < branches; b++) {
        const int dir = static_cast<int>((s.bits >> (20 + 2 * b)) % 4);
        const int by = top - 4 - 3 * b - static_cast<int>((s.bits >> (26 + b)) % 2);
        const int sx = DIRS[dir][0] < 0 ? -1 : 1, sz = DIRS[dir][1] < 0 ? -1 : 1;
        const int ex = DIRS[dir][0] + sx, ez = DIRS[dir][1] + sz; // two logs out
        s.log(DIRS[dir][0], by, DIRS[dir][1], JUNGLE_LOG);
        s.log(ex, by + 1, ez, JUNGLE_LOG);
        crownLayer(s, by + 1, 2 * ex, 2 * ez, 2.2f, 4, 3);
        crownLayer(s, by + 2, 2 * ex, 2 * ez, 1.5f, 0, 1);
    }
    // Crown, centred between the four trunk columns.
    const float r = 3.8f + static_cast<float>((s.bits >> 30) % 3) * 0.35f; // 3.8..4.5
    crownLayer(s, top - 1, 1, 1, r - 1.2f, 3, 5);
    crownLayer(s, top, 1, 1, r, 3, 6);
    crownLayer(s, top + 1, 1, 1, r - 1.0f, 0, 1);
    crownLayer(s, top + 2, 1, 1, r - 2.4f, 0, 1);
    return true;
}

// Middle-storey tree: 1×1 jungle trunk 5..9 tall, rounded crown radius
// 2..3 with a few vines.
bool jungleTree(const TreeSite& s) {
    const int trunkH = 5 + static_cast<int>((s.bits >> 8) % 5);
    const float r = 2.3f + static_cast<float>((s.bits >> 12) % 3) * 0.4f; // 2.3..3.1
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    s.claim(0);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, JUNGLE_LOG);
    crownLayer(s, top - 1, 0, 0, r, 3, 4);
    crownLayer(s, top, 0, 0, r, 4, 3);
    crownLayer(s, top + 1, 0, 0, r - 1.0f, 0, 1);
    s.leaf(0, top + 2, 0, LEAVES);
    return true;
}

// Floor bush: one log under a squat leaf mound (Minecraft's jungle bush).
bool bush(const TreeSite& s) {
    const int y = s.surface + 1;
    if (!s.fits(y + 2)) return false;
    s.claim(0);
    s.log(0, y, 0, JUNGLE_LOG);
    crownLayer(s, y, 0, 0, 2.2f, 0, 1);
    crownLayer(s, y + 1, 0, 0, 1.5f, 0, 1);
    if ((s.bits >> 12) % 2 == 0) s.leaf(0, y + 2, 0, LEAVES);
    return true;
}

bool growTree(const TreeSite& s) {
    const int pick = static_cast<int>((s.bits >> 40) % 100);
    if (pick < 9) return giantTree(s);
    if (pick < 36) return bush(s);
    return jungleTree(s);
}

block_type groundCover(const biomegen::GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    // Patches (8×8 cells) lean towards ferns or towards grass.
    const uint32_t patch = mix(g.seed, g.wx >> 3, 7, g.wz >> 3) % 3;
    const uint32_t fern = patch == 0 ? 2600 : patch == 1 ? 1200 : 500;
    const uint32_t roll = g.roll;
    if (roll < fern) return FERN;
    if (roll < fern + 3300) return TALL_GRASS;
    if (roll < fern + 3340) return POPPY;
    return AIR;
}

} // namespace

const BiomeDef RAINFOREST_DEF = {
    "Rainforest",
    GRASS, DIRT, SAND,
    0.75f, &growTree,
    nullptr,
    &groundCover,
    {0.58f, 1.08f, 0.55f}, // grass tint
    {0.48f, 1.32f, 0.26f}, // foliage tint
};
