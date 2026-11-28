// Swampland: low, flat, wet ground under murky olive grass. Shallow pools
// (one block of still water, some with a clay floor) are cut into the
// ground, wide swamp oaks trail strands of leaves from their canopy edge,
// and the open ground is thick with tall grass and ferns, with patches of
// blue orchids.

#include "biome_defs.h"

#include <cmath>
#include <cstdlib>

using biomegen::FeatureSite;
using biomegen::GroundSite;
using biomegen::TreeSite;

namespace {

uint64_t mix(uint64_t seed, int a, int b, uint64_t salt) {
    uint64_t v = seed ^ (salt * 0xD1B54A32D192ED03ULL) ^
                 (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32 | static_cast<uint32_t>(b));
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr uint64_t SALT_POOL = 0x5357'4150, SALT_POOL_EDGE = 0x5357'4151, SALT_ORCHID = 0x5357'4152;

// --- Pools ---------------------------------------------------------------
// One candidate pool per POOL_CELL² world cell: an ellipse with a ragged
// edge. Whether a column is pool depends only on its world position, so a
// pool that crosses a chunk border is cut the same way from both sides.

constexpr int POOL_CELL = 8;
constexpr int POOL_CHANCE = 70; // percent of cells with a pool

struct Pool {
    bool exists;
    float cx, cz;  // centre (world)
    float radius;
    float stretch; // x scale; z gets 1/stretch
    bool clay;     // clay floor in the middle
};

Pool poolOfCell(uint64_t seed, int cellX, int cellZ) {
    const uint64_t h = mix(seed, cellX, cellZ, SALT_POOL);
    Pool p{};
    p.exists = h % 100 < POOL_CHANCE;
    p.cx = static_cast<float>(cellX * POOL_CELL + 1) + static_cast<float>((h >> 8) % 60) / 10.0f;
    p.cz = static_cast<float>(cellZ * POOL_CELL + 1) + static_cast<float>((h >> 16) % 60) / 10.0f;
    p.radius = 1.8f + static_cast<float>((h >> 24) % 100) / 100.0f * 1.6f; // 1.8..3.4
    p.stretch = 0.7f + static_cast<float>((h >> 32) % 60) / 100.0f;        // 0.7..1.3
    p.clay = (h >> 40) % 100 < 40;
    return p;
}

enum class PoolCell : uint8_t { None, Water, ClayFloor };

PoolCell poolAt(uint64_t seed, int wx, int wz) {
    // Ragged edge: each column nudges the radius by up to ±0.6.
    const float jitter = static_cast<float>(mix(seed, wx, wz, SALT_POOL_EDGE) % 121) / 100.0f - 0.6f;
    PoolCell best = PoolCell::None;
    const int cellX = floorDiv(wx, POOL_CELL), cellZ = floorDiv(wz, POOL_CELL);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
            const Pool p = poolOfCell(seed, cellX + dx, cellZ + dz);
            if (!p.exists) continue;
            const float ex = (static_cast<float>(wx) + 0.5f - p.cx) * p.stretch;
            const float ez = (static_cast<float>(wz) + 0.5f - p.cz) / p.stretch;
            const float d = std::sqrt(ex * ex + ez * ez);
            if (d >= p.radius + jitter) continue;
            if (p.clay && d < p.radius - 1.0f) return PoolCell::ClayFloor;
            best = PoolCell::Water;
        }
    return best;
}

// Cuts the pools of this feature cell and its 8 neighbours (the ones inside
// the chunk). Covering the neighbours too means a cell whose own candidate
// was skipped (shaded by a tree) still gets its water; rewriting a column is
// harmless since it only turns GRASS into WATER.
// The column and its four neighbours are swamp: pools keep a rim of swamp
// grass and never spill into the next biome.
bool swampAround(const FeatureSite& s, int di, int dk) {
    return s.biomeAt(di, dk) == BIOME_SWAMPLAND && s.biomeAt(di + 1, dk) == BIOME_SWAMPLAND &&
           s.biomeAt(di - 1, dk) == BIOME_SWAMPLAND && s.biomeAt(di, dk + 1) == BIOME_SWAMPLAND &&
           s.biomeAt(di, dk - 1) == BIOME_SWAMPLAND;
}

void decorate(const FeatureSite& s) {
    constexpr int CELL = 4; // terrainfill::TREE_CELL, the feature grid
    const int cellI = s.i - (s.wx - floorDiv(s.wx, CELL) * CELL);
    const int cellK = s.k - (s.wz - floorDiv(s.wz, CELL) * CELL);
    for (int ci = cellI - CELL; ci < cellI + 2 * CELL; ci++) {
        if (ci < 0 || ci >= CHUNK_SIZE) continue;
        for (int ck = cellK - CELL; ck < cellK + 2 * CELL; ck++) {
            if (ck < 0 || ck >= CHUNK_SIZE) continue;
            const int di = ci - s.i, dk = ck - s.k;
            const PoolCell pc = poolAt(s.seed, s.wx + di, s.wz + dk);
            if (pc == PoolCell::None || !swampAround(s, di, dk)) continue;
            // The open grass surface of that column (swamps are flat, so it is
            // within a few blocks of this cell's).
            int y = -1;
            for (int t = s.surface + 3; t >= s.surface - 3; t--) {
                if (s.atNear(di, t, dk) == GRASS && s.atNear(di, t + 1, dk) == AIR) {
                    y = t;
                    break;
                }
            }
            if (y < 0 || y <= s.seaLevel) continue;
            // Keep the water in: no air beside it inside the chunk (a step down)
            // or under it (a cave).
            const block_type below = s.atNear(di, y - 1, dk);
            if (below == AIR || hasFlag(below, BF_LIQUID)) continue;
            bool leaks = false;
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& n : nb) {
                const int ni = ci + n[0], nk = ck + n[1];
                if (ni < 0 || ni >= CHUNK_SIZE || nk < 0 || nk >= CHUNK_SIZE) continue;
                const block_type side = s.atNear(di + n[0], y, dk + n[1]);
                if (side == AIR || hasFlag(side, BF_CROSS)) leaks = true;
            }
            if (leaks) continue;
            s.replace(di, y, dk, WATER);
            if (pc == PoolCell::ClayFloor && below == DIRT) s.replace(di, y - 1, dk, CLAY);
        }
    }
}

// --- Swamp oak -------------------------------------------------------------
// Minecraft's swamp tree: a short trunk under a wide, flat canopy, with
// strands of leaves hanging from the canopy rim.

constexpr int SWAMP_OAK_RADIUS = 3;

bool growTree(const TreeSite& s) {
    const int trunkH = 4 + static_cast<int>((s.bits >> 8) % 3); // 4..6
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    s.claim(SWAMP_OAK_RADIUS);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, WOOD);
    // Two wide layers (radius 3, corners cut), a radius-2 layer, a cap.
    for (int ly = -2; ly <= 1; ly++) {
        const int r = ly <= -1 ? 3 : ly == 0 ? 2 : 1;
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                const int ax = std::abs(dx), az = std::abs(dz);
                if (r >= 2 && ax == r && az == r) continue;                         // square corners
                if (r == 3 && ax + az == 5 && ((s.bits >> (ax * 4 + az)) & 1)) continue; // ragged rim
                if (r == 1 && ax + az == 2) continue;
                s.leaf(dx, top + ly, dz, LEAVES);
            }
    }
    // Strands: from rim cells of the bottom layer, 1..3 leaves hanging down,
    // stopping two blocks above the ground.
    const int lowest = top - 2;
    uint64_t bits = s.bits;
    for (int dx = -3; dx <= 3; dx++)
        for (int dz = -3; dz <= 3; dz++) {
            if (std::abs(dx) != 3 && std::abs(dz) != 3) continue;
            if (std::abs(dx) == 3 && std::abs(dz) == 3) continue;
            bits = bits * 6364136223846793005ULL + 1442695040888963407ULL;
            const int roll = static_cast<int>((bits >> 33) % 100);
            if (roll >= 45) continue;
            const int len = 1 + roll % 3;
            for (int d = 1; d <= len && lowest - d >= s.surface + 2; d++) s.leaf(dx, lowest - d, dz, LEAVES);
        }
    return true;
}

// --- Ground cover ----------------------------------------------------------

block_type groundCover(const GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    // Blue orchids grow in scattered patches, as in Minecraft swamps.
    const bool orchidPatch = mix(g.seed, floorDiv(g.wx, 8), floorDiv(g.wz, 8), SALT_ORCHID) % 100 < 22;
    if (orchidPatch && g.roll < 450) return BLUE_ORCHID;
    if (g.roll < 2600) return TALL_GRASS;
    if (g.roll < 3500) return FERN;
    return AIR;
}

} // namespace

const BiomeDef SWAMPLAND_DEF = {
    "Swampland",
    GRASS, DIRT, DIRT,
    0.12f, &growTree,
    &decorate,
    &groundCover,
    {0.98f, 0.76f, 1.02f}, // grass tint: murky olive (~#4C5D28 on the stock grass)
    {1.06f, 0.80f, 1.14f}, // foliage tint: dark olive leaves
};
