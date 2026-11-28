// Shrubland: temperate but dry scrub (Minecraft Beta's shrubland). Low
// bushes (a one-log stump in a small leaf blob) in loose clusters instead
// of trees, a rare short oak, worn ground where coarse dirt and gravel show
// through the grass, dead bushes on the bare patches and sparse tall grass.
// A dusty olive green between the plains' green and the savanna's straw.

#include "biome_defs.h"
#include "tree_shapes.h"

#include <cstdint>
#include <cstdlib>

namespace {

// splitmix64 over (seed, coarse cell, salt) for patches and clusters.
uint64_t mix(uint64_t seed, int x, int z, uint64_t salt) {
    uint64_t v = seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^ static_cast<uint32_t>(z) ^
                 (salt * 0xD1B54A32D192ED03ULL);
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

constexpr uint64_t SALT_SCRUB = 0x5348, SALT_BARE = 0x5349, SALT_JITTER = 0x534A, SALT_ROLL = 0x534B;

// 0..99: how thick the scrub is around (wx, wz), constant over 16-block
// cells and smoothed by averaging the four cells the column sits between,
// so bush clusters fade in and out instead of switching at cell lines.
int scrubLevel(uint64_t seed, int wx, int wz, uint64_t salt) {
    constexpr int CELL = 16;
    const int fx = wx + CELL / 2, fz = wz + CELL / 2;
    const int cx = fx >= 0 ? fx / CELL : -((-fx + CELL - 1) / CELL);
    const int cz = fz >= 0 ? fz / CELL : -((-fz + CELL - 1) / CELL);
    const int tx = fx - cx * CELL, tz = fz - cz * CELL; // 0..15 inside the cell
    auto v = [&](int x, int z) { return static_cast<int>(mix(seed, x, z, salt) % 100); };
    const int a = v(cx - 1, cz - 1), b = v(cx, cz - 1), c = v(cx - 1, cz), d = v(cx, cz);
    const int top = a * (CELL - tx) + b * tx, bottom = c * (CELL - tx) + d * tx;
    return (top * (CELL - tz) + bottom * tz) / (CELL * CELL);
}

// Bush: a one-log stump (rarely two) wrapped in a leaf blob, radius 1 or 2,
// with a few corner leaves left out so no two look alike.
bool bush(const biomegen::TreeSite& s) {
    const bool big = (s.bits >> 4) % 100 < 25;
    const int stump = (s.bits >> 8) % 100 < 15 ? 2 : 1;
    const int base = s.surface + 1;
    const int top = base + stump; // highest leaf layer
    if (!s.fits(top + 1)) return false;
    const int radius = big ? 2 : 1;
    s.claim(radius);
    for (int y = base; y < base + stump; y++) s.log(0, y, 0, WOOD);
    uint64_t holes = s.bits >> 16; // one bit per optional leaf
    auto maybe = [&](int dx, int y, int dz) {
        const bool keep = (holes & 1u) != 0;
        holes = (holes >> 1) | (holes << 63);
        if (keep) s.leaf(dx, y, dz, LEAVES);
    };
    if (big) {
        // Wide bottom layer (radius 2, rounded), 3×3 above, a plus on top.
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++) {
                const int d = std::abs(dx) + std::abs(dz);
                if (d >= 4) continue;
                if (d == 3) maybe(dx, base + stump - 1, dz);
                else s.leaf(dx, base + stump - 1, dz, LEAVES);
            }
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                if (std::abs(dx) == 1 && std::abs(dz) == 1) maybe(dx, top, dz);
                else s.leaf(dx, top, dz, LEAVES);
            }
        s.leaf(0, top + 1, 0, LEAVES);
    } else {
        // 3×3 around the top of the stump, a plus (with gaps) above.
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++) {
                if (std::abs(dx) == 1 && std::abs(dz) == 1) maybe(dx, top - 1, dz);
                else s.leaf(dx, top - 1, dz, LEAVES);
            }
        s.leaf(0, top, 0, LEAVES);
        maybe(1, top, 0);
        maybe(-1, top, 0);
        maybe(0, top, 1);
        maybe(0, top, -1);
    }
    return true;
}

// Short, round oak: the odd real tree standing over the scrub.
bool shortOak(const biomegen::TreeSite& s) {
    biomegen::TreeSite low = s;
    low.bits = (s.bits & ~(uint64_t{0xF} << 8)) | (uint64_t{(s.bits >> 8) % 2} << 8); // trunk 4..5
    return trees::oak(low);
}

bool growTree(const biomegen::TreeSite& s) {
    const int roll = static_cast<int>(mix(s.seed, s.wx, s.wz, SALT_ROLL) % 1000);
    // Bushes come in clusters: thick where the scrub level is high, a lone
    // one here and there elsewhere.
    const int scrub = scrubLevel(s.seed, s.wx, s.wz, SALT_SCRUB);
    // A rare short oak, only among the thicker scrub.
    if (roll < 5) return scrub >= 45 && shortOak(s);
    const int chance = scrub < 35 ? 50 : scrub < 60 ? 170 : 380; // per mille
    if (roll >= chance) return false;
    return bush(s);
}

// Is this column inside a bare patch centred at the feature site? Distance
// with per-column jitter, so the edge is ragged rather than a disc.
bool inPatch(const biomegen::FeatureSite& f, int dx, int dz, int radius) {
    const uint64_t j = mix(f.seed, f.wx + dx, f.wz + dz, SALT_JITTER);
    const int d2 = dx * dx + dz * dz;
    return d2 * 4 + static_cast<int>(j % 9) <= radius * radius * 4 + 4;
}

// Worn ground: patches of coarse dirt (sometimes gravel) where the grass has
// died back, commoner in the dry stretches (low scrub level). Kept whole
// inside the chunk so no patch is cut straight at a chunk border, and only
// on shrubland columns so none spills into a neighbouring biome.
void decorate(const biomegen::FeatureSite& f) {
    if (f.ground != GRASS || f.surface <= f.seaLevel + 1) return;
    const int dry = 100 - scrubLevel(f.seed, f.wx, f.wz, SALT_BARE);
    const int roll = static_cast<int>((f.bits >> 8) % 100);
    if (roll >= (dry > 60 ? 45 : dry > 35 ? 20 : 6)) return;
    const int radius = 1 + static_cast<int>((f.bits >> 16) % 3); // 1..3
    if (f.i < radius || f.k < radius || f.i >= CHUNK_SIZE - radius || f.k >= CHUNK_SIZE - radius) return;
    const block_type bare = (f.bits >> 24) % 100 < 22 ? GRAVEL : COARSE_DIRT;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dz = -radius; dz <= radius; dz++) {
            if (!inPatch(f, dx, dz, radius) || f.biomeAt(dx, dz) != BIOME_SHRUBLAND) continue;
            // The column's own grass top, within a step of the site.
            for (int y = f.surface + 1; y >= f.surface - 1; y--) {
                const block_type t = f.atNear(dx, y, dz);
                if (t == AIR) continue;
                if (t == GRASS && f.atNear(dx, y + 1, dz) == AIR) f.replace(dx, y, dz, bare);
                break;
            }
        }
}

block_type groundCover(const biomegen::GroundSite& g) {
    const uint32_t roll = g.roll;
    if (g.ground == COARSE_DIRT) return roll < 900 ? DEAD_BUSH : AIR;
    if (g.ground == GRAVEL || g.ground == SAND) return roll < 250 ? DEAD_BUSH : AIR;
    if (g.ground != GRASS) return AIR;
    // Tall grass in tufts: thicker in some stretches than others.
    const int tuft = scrubLevel(g.seed, g.wx, g.wz, SALT_SCRUB ^ 0xC0FFEE);
    const uint32_t grass = tuft > 60 ? 1400 : tuft > 35 ? 700 : 300;
    if (roll < grass) return TALL_GRASS;
    if (roll < grass + 40) return DEAD_BUSH;
    if (roll < grass + 60) return DANDELION;
    return AIR;
}

} // namespace

const BiomeDef SHRUBLAND_DEF = {
    "Shrubland",
    GRASS, DIRT, SAND,
    0.6f, &growTree,
    &decorate,
    &groundCover,
    {1.24f, 0.88f, 0.62f}, // grass tint: dusty olive
    {1.18f, 0.90f, 0.64f}, // foliage tint: dry olive bushes
};
