// Taiga: cool spruce woods. Three spruce shapes (the stock stepped cone, a
// tall thin pine-like one and an occasional giant 2x2-trunk spruce), a
// forest floor of grass with podzol patches (most of all in the shade of
// the spruces), small mossy-cobblestone boulders, and ferns.

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "biome_defs.h"
#include "terrain_fill.h"
#include "tree_shapes.h"

using biomegen::FeatureSite;
using biomegen::GroundSite;
using biomegen::TreeSite;

namespace {

// --- Hashing (deterministic from the world position and seed) ---

uint64_t mix(uint64_t v) {
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

uint64_t hashAt(uint64_t seed, int x, int z, uint64_t salt) {
    return mix(seed * 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^
               static_cast<uint32_t>(z) ^ (salt * 0xD1B54A32D192ED03ULL));
}

double unit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr int CELL = terrainfill::TREE_CELL; // feature sites come one per CELL² world cell
constexpr uint64_t SALT_PODZOL = 0x7A16A01, SALT_PODZOL_FINE = 0x7A16A02, SALT_FERNS = 0x7A16A03,
                   SALT_GROVES = 0x7A16A04;

// Smoothed value noise on a `cell`-block lattice, 0..1: blobby patches that
// are the same whichever chunk asks.
double valueNoise(uint64_t seed, int wx, int wz, int cell, uint64_t salt) {
    const int cx = floorDiv(wx, cell), cz = floorDiv(wz, cell);
    auto fade = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double fx = fade(static_cast<double>(wx - cx * cell) / cell);
    const double fz = fade(static_cast<double>(wz - cz * cell) / cell);
    const double a = unit(hashAt(seed, cx, cz, salt)), b = unit(hashAt(seed, cx + 1, cz, salt));
    const double c = unit(hashAt(seed, cx, cz + 1, salt)), d = unit(hashAt(seed, cx + 1, cz + 1, salt));
    return (a + (b - a) * fx) * (1.0 - fz) + (c + (d - c) * fx) * fz;
}

// Podzol: large soft patches with ragged edges. Shaded ground (under a
// spruce) turns to podzol far more readily, as needle litter piles up there.
bool podzolAt(uint64_t seed, int wx, int wz, bool shaded) {
    const double f = 0.75 * valueNoise(seed, wx, wz, 9, SALT_PODZOL) +
                     0.25 * valueNoise(seed, wx, wz, 3, SALT_PODZOL_FINE);
    return f > (shaded ? 0.42 : 0.58);
}

// --- Trees ---

bool isSpruce(block_type t) {
    return t == SPRUCE_LOG || t == SPRUCE_LEAVES;
}

// Square ring of leaves of radius r around the trunk column, corners
// trimmed when r > 1 (r = 0: just the centre).
void disc(const TreeSite& s, int y, int r, bool skipCenter) {
    for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (r > 1 && std::abs(dx) == r && std::abs(dz) == r) continue;
            if (skipCenter && dx == 0 && dz == 0) continue;
            s.leaf(dx, y, dz, SPRUCE_LEAVES);
        }
}

// Tall thin spruce (Minecraft's "pine" variant): a long bare trunk with a
// short narrow crown of alternating radius 1 / 2 rings at the top.
bool tallSpruce(const TreeSite& s) {
    const int trunkH = 9 + static_cast<int>((s.bits >> 8) % 4);  // 9..12
    const int crown = 4 + static_cast<int>((s.bits >> 12) % 3);  // 4..6 leaf layers
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    s.claim(3);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, SPRUCE_LOG);
    s.leaf(0, top + 1, 0, SPRUCE_LEAVES);
    s.leaf(0, top + 2, 0, SPRUCE_LEAVES);
    // Plus shape right under the cap, then 1 / 2 / 1 / 2 ... downward.
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            if (std::abs(dx) + std::abs(dz) == 1) s.leaf(dx, top, dz, SPRUCE_LEAVES);
    for (int d = 1; d < crown; d++) disc(s, top - d, d % 2 == 0 ? 2 : 1, true);
    return true;
}

// Giant spruce on a 2x2 trunk (columns 0..1 on both axes): 14..20 tall, a
// conical crown over the upper ~60% that widens to radius 3..4 around the
// trunk (so leaves reach at most 5 columns from the trunk column).
bool giantSpruce(const TreeSite& s) {
    const int trunkH = 14 + static_cast<int>((s.bits >> 8) % 7);  // 14..20
    const int maxR = 3 + static_cast<int>((s.bits >> 12) % 2);    // 3..4
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    s.claim(maxR + 2);
    // The trunk starts one below the surface so it meets the ground on a
    // column up to 2 lower than the trunk column (slope rule in terrain_fill).
    for (int y = s.surface - 1; y <= top; y++)
        for (int dx = 0; dx <= 1; dx++)
            for (int dz = 0; dz <= 1; dz++) s.log(dx, y, dz, SPRUCE_LOG);
    // Box ring of radius r around the 2x2 trunk.
    auto ring = [&](int y, int r) {
        for (int dx = -r; dx <= r + 1; dx++)
            for (int dz = -r; dz <= r + 1; dz++) {
                const int ex = dx <= 0 ? -dx : dx - 1, ez = dz <= 0 ? -dz : dz - 1;
                if (r > 1 && ex == r && ez == r) continue;
                s.leaf(dx, y, dz, SPRUCE_LEAVES);
            }
    };
    ring(top + 1, 0);
    ring(top + 2, 0);
    const int span = trunkH * 3 / 5;
    for (int d = 0; d <= span; d++) {
        int r = 1 + d * maxR / span;
        if (d % 3 == 2 && r > 1) r--; // stepped silhouette
        ring(top - d, std::min(r, maxR));
    }
    return true;
}

// Groves and glades: a 20-block value-noise field decides how many of the
// candidate trees grow (none in a glade, all in a dense stand), so the woods
// open up in places instead of reading as one solid wall of spruce.
bool inGlade(const TreeSite& s) {
    const double g = valueNoise(s.seed, s.wx, s.wz, 20, SALT_GROVES);
    const double keep = std::clamp((g - 0.2) / 0.45, 0.0, 1.0); // 0 below 0.2, 1 above 0.65
    return unit(mix(s.bits ^ 0x61ADE5ULL)) >= keep;
}

bool growTree(const TreeSite& s) {
    if (inGlade(s)) return false;
    const auto pick = static_cast<int>((s.bits >> 40) % 100);
    if (pick < 6 && giantSpruce(s)) return true;
    if (pick < 36) return tallSpruce(s);
    return trees::spruce(s);
}

// --- Features: podzol floor + mossy boulders ---

// Paints podzol over the grass of the columns in the world rectangle
// [x0, x1] x [z0, z1] (inside the chunk) where the podzol field says so.
void paintPodzol(const FeatureSite& s, int x0, int x1, int z0, int z1) {
    for (int wx = x0; wx <= x1; wx++)
        for (int wz = z0; wz <= z1; wz++) {
            const int di = wx - s.wx, dk = wz - s.wz;
            bool shaded = false;
            for (int y = std::min(s.surface + 24, CHUNK_HEIGHT - 1); y >= s.surface - 10 && y > 0; y--) {
                const block_type t = s.atNear(di, y, dk);
                if (isSpruce(t)) shaded = true;
                if (t == GRASS) {
                    if (podzolAt(s.seed, wx, wz, shaded)) s.replace(di, y, dk, PODZOL);
                    break;
                }
                if (t != AIR && !isSpruce(t) && !hasFlag(t, BF_CROSS)) break; // other ground / water
            }
        }
}

// A small mossy boulder: 2..4 blocks around the site, sunk into the ground
// where the terrain drops away. Kept one column inside the chunk.
void boulder(const FeatureSite& s) {
    if (s.i < 1 || s.i > CHUNK_SIZE - 2 || s.k < 1 || s.k > CHUNK_SIZE - 2) return;
    if (s.surface <= s.seaLevel + 1 || s.surface + 3 >= CHUNK_HEIGHT) return;
    const uint64_t b = s.bits >> 16;
    auto stone = [&](int n) { return ((b >> (20 + n)) & 3) == 0 ? COBBLESTONE : MOSSY_COBBLESTONE; };
    auto put = [&](int di, int y, int dk, block_type t) {
        const block_type here = s.atNear(di, y, dk);
        if (here != AIR && !hasFlag(here, BF_CROSS)) return false;
        s.replace(di, y, dk, t);
        // Fill down to the ground (at most 2) so nothing floats.
        for (int d = 1; d <= 2; d++) {
            const block_type under = s.atNear(di, y - d, dk);
            if (under != AIR && !hasFlag(under, BF_CROSS)) break;
            s.replace(di, y - d, dk, t);
        }
        return true;
    };
    // Sink the centre one block into the ground.
    s.replace(0, s.surface, 0, stone(0));
    put(0, s.surface + 1, 0, stone(1));
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int first = static_cast<int>(b % 4);
    const int sides = 1 + static_cast<int>((b >> 2) % 2); // 1..2
    for (int n = 0; n < sides; n++) {
        const auto& d = dirs[(first + n * 3) % 4]; // two sides: an L
        put(d[0], s.surface + 1, d[1], stone(2 + n));
    }
    if (((b >> 4) % 3) == 0) put(0, s.surface + 2, 0, stone(5)); // occasional taller one
    s.claim();
}

void decorate(const FeatureSite& s) {
    // Each feature cell paints its own 4x4 cell and one cell around it (clipped
    // to the chunk): a cell whose site sits under a canopy is skipped by
    // terrain_fill, and its neighbours fill it in. The paint is a pure
    // function of the column, so overlapping paints agree.
    const int cx = floorDiv(s.wx, CELL) * CELL;
    const int cz = floorDiv(s.wz, CELL) * CELL;
    const int ox = s.wx - s.i, oz = s.wz - s.k; // chunk origin
    paintPodzol(s, std::max(cx - CELL, ox), std::min(cx + 2 * CELL - 1, ox + CHUNK_SIZE - 1),
                std::max(cz - CELL, oz), std::min(cz + 2 * CELL - 1, oz + CHUNK_SIZE - 1));

    if ((s.ground == GRASS || s.ground == PODZOL) && (s.bits >> 8) % 100 < 5) boulder(s);
}

// --- Ground cover: ferns in clumps, some grass, sparser on podzol ---

block_type groundCover(const GroundSite& g) {
    if (g.ground != GRASS && g.ground != PODZOL) return AIR;
    // Fern density varies in 6-block clumps (x0.3 .. x1.7).
    const double clump = 0.3 + 1.4 * valueNoise(g.seed, g.wx, g.wz, 6, SALT_FERNS);
    const auto fern = static_cast<uint32_t>((g.ground == PODZOL ? 1400.0 : 1100.0) * clump);
    const uint32_t grass = g.ground == PODZOL ? 300 : 900;
    if (g.roll < fern) return FERN;
    if (g.roll < fern + grass) return TALL_GRASS;
    return AIR;
}

} // namespace

const BiomeDef TAIGA_DEF = {
    "Taiga",
    GRASS, DIRT, SAND,
    0.45f, &growTree, // before glades thin it (inGlade): ~0.3 on average
    &decorate,
    &groundCover,
    {0.84f, 0.90f, 1.50f}, // grass tint: cool muted blue-green, between forest and tundra
    {0.87f, 0.96f, 1.90f}, // foliage tint (oak leaves only; spruce keeps its own)
};
