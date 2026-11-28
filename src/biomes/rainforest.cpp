// Rainforest: the hot, wet corner of the climate table. A layered jungle:
// emergent giants on 2×2 jungle-log trunks with broad flat crowns, branch
// clusters and buttress roots; a middle storey of smaller jungle trees; low
// lumpy jungle-leaf bushes covering most of the floor; vines draped down
// crown rims, branch balls and trunks; and deep green grass with tall grass
// (ferns only here and there) between the bushes.

#include <cstdint>
#include <cstdlib>

#include "biome_defs.h"
#include "tree_shapes.h"
#include "vine.h"

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

constexpr int R = biomegen::TREE_MAX_RADIUS;

// Vine length: mostly short (1..6), one in five a long one reaching far
// down (7..14).
int vineLength(uint32_t h) {
    return (h >> 24) % 5 == 0 ? 7 + static_cast<int>((h >> 12) % 8) : 1 + static_cast<int>((h >> 12) % 6);
}

// A disc of leaves at height y around a centre given in half blocks (cx2,
// cz2 = 2 × centre), radius r, with rim cells dropped at random for a
// ragged crown. Pure, so the vine pass can ask which cells are leaves
// without reading blocks (which may lie in another chunk).
struct Crown {
    int y, cx2, cz2;
    float r;

    bool has(const TreeSite& s, int dx, int dz) const {
        if (std::abs(dx) > R || std::abs(dz) > R) return false;
        const float fx = static_cast<float>(2 * dx - cx2) * 0.5f;
        const float fz = static_cast<float>(2 * dz - cz2) * 0.5f;
        const float d2 = fx * fx + fz * fz;
        if (d2 > r * r) return false;
        const bool rim = d2 > (r - 1.0f) * (r - 1.0f);
        return !(rim && mix(s.bits, dx, y, dz) % 4 == 0);
    }
    template <typename F> void forEach(F&& f) const {
        const int reach = static_cast<int>(r) + 1;
        const int cx = cx2 / 2, cz = cz2 / 2;
        for (int dx = cx - reach; dx <= cx + reach + 1; dx++)
            for (int dz = cz - reach; dz <= cz + reach + 1; dz++) f(dx, dz);
    }
    void draw(const TreeSite& s) const {
        forEach([&](int dx, int dz) {
            if (has(s, dx, dz)) s.leaf(dx, y, dz, JUNGLE_LEAVES);
        });
    }
    // Vines down the outside of the rim: for each leaf with open air beside
    // it (outside this layer), 1 in `odds` sides gets a chain lying against
    // that leaf's face.
    void drapeVines(const TreeSite& s, int odds) const {
        forEach([&](int dx, int dz) {
            if (!has(s, dx, dz)) return;
            for (int b = 0; b < 4; b++) {
                const int vx = dx + vine::SIDE[b][0], vz = dz + vine::SIDE[b][2];
                if (std::abs(vx) > R || std::abs(vz) > R || has(s, vx, vz)) continue;
                const uint32_t h = mix(s.bits ^ 0x56494E45ULL, vx, y, vz * 4 + b);
                if (h % static_cast<uint32_t>(odds) != 0) continue;
                // The vine cell's side facing back at the leaf: the opposite bit.
                s.vine(vx, y, vz, static_cast<uint8_t>(1u << (b ^ 1)), vineLength(h));
            }
        });
    }
};

// Vines climbing a trunk side: chains against the log at (lx, lz), hung
// from just under the crown.
void trunkVines(const TreeSite& s, int lx, int lz, int fromY, int odds, int salt) {
    for (int b = 0; b < 4; b++) {
        const int vx = lx + vine::SIDE[b][0], vz = lz + vine::SIDE[b][2];
        const uint32_t h = mix(s.bits ^ 0x5452554EULL, vx, salt, vz * 4 + b);
        if (h % static_cast<uint32_t>(odds) != 0) continue;
        const int top = fromY - static_cast<int>((h >> 8) % 3);
        s.vine(vx, top, vz, static_cast<uint8_t>(1u << (b ^ 1)), 4 + static_cast<int>((h >> 16) % 12));
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
    // Crown, centred between the four trunk columns.
    const float r = 3.8f + static_cast<float>((s.bits >> 30) % 3) * 0.35f; // 3.8..4.5
    const Crown crown[4] = {
        {top - 1, 1, 1, r - 1.2f}, {top, 1, 1, r}, {top + 1, 1, 1, r - 1.0f}, {top + 2, 1, 1, r - 2.4f}};
    // Branch clusters: a short diagonal limb with a leaf ball at its end.
    static const int DIRS[4][2] = {{-1, -1}, {2, -1}, {-1, 2}, {2, 2}};
    const int branches = 1 + static_cast<int>((s.bits >> 16) % 2);
    Crown balls[2][2];
    for (int b = 0; b < branches; b++) {
        const int dir = static_cast<int>((s.bits >> (20 + 2 * b)) % 4);
        const int by = top - 4 - 3 * b - static_cast<int>((s.bits >> (26 + b)) % 2);
        const int sx = DIRS[dir][0] < 0 ? -1 : 1, sz = DIRS[dir][1] < 0 ? -1 : 1;
        const int ex = DIRS[dir][0] + sx, ez = DIRS[dir][1] + sz; // two logs out
        s.log(DIRS[dir][0], by, DIRS[dir][1], JUNGLE_LOG);
        s.log(ex, by + 1, ez, JUNGLE_LOG);
        balls[b][0] = {by + 1, 2 * ex, 2 * ez, 2.2f};
        balls[b][1] = {by + 2, 2 * ex, 2 * ez, 1.5f};
        balls[b][0].draw(s);
        balls[b][1].draw(s);
    }
    for (const Crown& c : crown) c.draw(s);
    // Vines once every leaf is in place, so none ends up inside the crown.
    crown[1].drapeVines(s, 2);
    crown[0].drapeVines(s, 3);
    for (int b = 0; b < branches; b++) balls[b][0].drapeVines(s, 3);
    for (int dx = 0; dx <= 1; dx++)
        for (int dz = 0; dz <= 1; dz++) trunkVines(s, dx, dz, top - 2, 3, dx * 2 + dz);
    return true;
}

// Middle-storey tree: 1×1 jungle trunk 5..9 tall, rounded crown radius
// 2..3 with vines on its rim and trunk.
bool jungleTree(const TreeSite& s) {
    const int trunkH = 5 + static_cast<int>((s.bits >> 8) % 5);
    const float r = 2.3f + static_cast<float>((s.bits >> 12) % 3) * 0.4f; // 2.3..3.1
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    s.claim(0);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, JUNGLE_LOG);
    const Crown crown[3] = {{top - 1, 0, 0, r}, {top, 0, 0, r}, {top + 1, 0, 0, r - 1.0f}};
    for (const Crown& c : crown) c.draw(s);
    s.leaf(0, top + 2, 0, JUNGLE_LEAVES);
    crown[0].drapeVines(s, 3);
    trunkVines(s, 0, 0, top - 2, 4, 0);
    return true;
}

// Floor bush (Minecraft's jungle bush): a jungle log hidden in a lumpy
// mound of jungle leaves, about 3×3 and 1..2 high: a 3×3 layer with
// random corners missing and a few lumps sticking out a block further,
// under a smaller, uneven top. `leaf(dx, y, dz)` / `log(dx, y, dz)` write
// relative to the bush's centre column; y is the layer over the ground.
template <typename Leaf, typename Log> void bushShape(uint64_t bits, int y, Leaf&& leaf, Log&& log) {
    log(0, y, 0);
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++) {
            const int ax = std::abs(dx), az = std::abs(dz);
            const uint32_t h = mix(bits, dx, 1, dz);
            bool lower = false;
            if (ax <= 1 && az <= 1)
                lower = ax + az < 2 || h % 5 < 3; // 3×3, corners 3 in 5
            else if (ax + az == 2)
                lower = h % 3 == 0; // lumps on the sides
            if (lower) leaf(dx, y, dz);
            bool upper = false;
            if (ax + az == 0)
                upper = true; // over the log
            else if (ax + az == 1)
                upper = (h >> 8) % 4 != 0; // most of the plus
            else if (ax == 1 && az == 1)
                upper = (h >> 8) % 5 == 0; // a rare corner
            if (upper) leaf(dx, y + 1, dz);
        }
    if ((bits >> 12) % 4 == 0) leaf(0, y + 2, 0); // a taller one now and then
}

bool bush(const TreeSite& s) {
    const int y = s.surface + 1;
    if (!s.fits(y + 2)) return false;
    s.claim(0);
    bushShape(
        s.bits, y, [&](int dx, int ly, int dz) { s.leaf(dx, ly, dz, JUNGLE_LEAVES); },
        [&](int dx, int ly, int dz) { s.log(dx, ly, dz, JUNGLE_LOG); });
    return true;
}

bool growTree(const TreeSite& s) {
    // Mostly undergrowth: the floor between the trunks is a sea of bushes.
    const int pick = static_cast<int>((s.bits >> 40) % 100);
    if (pick < 10) return giantTree(s);
    if (pick < 55) return bush(s);
    return jungleTree(s);
}

// More bushes between the trees: up to three per feature cell, at the site
// and two diagonal steps off it, on open grass. Features only write inside the
// chunk, so a mound's 3×3 core keeps a column away from the chunk edge
// (only an outer lump can be cut), and lumps stay on rainforest columns.
void featureBush(const biomegen::FeatureSite& f, int di, int dk, uint64_t bits) {
    const int ci = f.i + di, ck = f.k + dk;
    if (ci < 1 || ci > CHUNK_SIZE - 2 || ck < 1 || ck > CHUNK_SIZE - 2) return;
    if (f.biomeAt(di, dk) != BIOME_RAINFOREST) return;
    // That column's open grass surface (near this cell's).
    int y = -1;
    for (int t = f.surface + 2; t >= f.surface - 2; t--)
        if (f.atNear(di, t, dk) == GRASS && f.atNear(di, t + 1, dk) == AIR) {
            y = t + 1;
            break;
        }
    if (y < 0 || y + 2 >= CHUNK_HEIGHT) return;
    bushShape(
        bits, y,
        [&](int dx, int ly, int dz) {
            if (f.biomeAt(di + dx, dk + dz) == BIOME_RAINFOREST) f.setAt(di + dx, ly, dk + dz, JUNGLE_LEAVES);
        },
        [&](int dx, int ly, int dz) { f.setAt(di + dx, ly, dk + dz, JUNGLE_LOG); });
}

void decorate(const biomegen::FeatureSite& f) {
    if (f.ground != GRASS) return;
    if (f.bits % 8 != 0) featureBush(f, 0, 0, f.bits >> 3);
    const int dirs[4][2] = {{2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
    const int* d = dirs[(f.bits >> 40) % 4];
    if ((f.bits >> 42) % 4 != 0) featureBush(f, d[0], d[1], f.bits >> 20);
    if ((f.bits >> 44) % 2 != 0) featureBush(f, -d[0], d[1], f.bits >> 30);
}

block_type groundCover(const biomegen::GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    // Mostly tall grass between the bushes; ferns (which read as little
    // fir saplings) only now and then.
    const uint32_t roll = g.roll;
    if (roll < 250) return FERN;
    if (roll < 8000) return TALL_GRASS;
    if (roll < 8060) return POPPY;
    return AIR;
}

} // namespace

const BiomeDef RAINFOREST_DEF = {
    "Rainforest",          GRASS, DIRT, SAND, 0.85f, &growTree, &decorate, &groundCover,
    {0.45f, 0.85f, 0.40f}, // grass tint: deep jungle green
    {0.48f, 1.32f, 0.26f}, // foliage tint
};
