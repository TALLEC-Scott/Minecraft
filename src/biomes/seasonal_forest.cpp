// Seasonal forest: warm, sunlit deciduous woods between the savanna and the
// jungle (Minecraft Beta's seasonal forest). Lighter than the Forest:
// oak and birch stands broken up by open clearings full of flowers, and a
// touch of autumn: some crowns have turned red or orange among the green.
//
// Patches (clearings, stands of turned trees, flower kinds) come from a
// smooth value noise over coarse world cells, so their edges are organic
// and identical on both sides of a chunk border.

#include "biome_defs.h"
#include "biomes/seasonal_forest.h"
#include "tree_shapes.h"

#include <cmath>
#include <cstdlib>

using biomegen::FeatureSite;
using biomegen::GroundSite;
using biomegen::TreeSite;

namespace {

uint64_t mix(uint64_t v) {
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

uint64_t hashCell(uint64_t seed, int x, int z, uint64_t salt) {
    return mix(seed ^ mix((static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32 | static_cast<uint32_t>(z)) ^
                          (salt * 0xD1B54A32D192ED03ULL)));
}

double unit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Smooth 0..1 value noise: hashed values on a `cell`-block grid, blended
// with smoothstep. Cheap (four hashes), deterministic per world column.
double patchNoise(uint64_t seed, int wx, int wz, int cell, uint64_t salt) {
    const int cx = floorDiv(wx, cell), cz = floorDiv(wz, cell);
    double fx = (wx - cx * cell + 0.5) / cell, fz = (wz - cz * cell + 0.5) / cell;
    fx = fx * fx * (3.0 - 2.0 * fx);
    fz = fz * fz * (3.0 - 2.0 * fz);
    const double a = unit(hashCell(seed, cx, cz, salt)), b = unit(hashCell(seed, cx + 1, cz, salt));
    const double c = unit(hashCell(seed, cx, cz + 1, salt)), d = unit(hashCell(seed, cx + 1, cz + 1, salt));
    return (a + (b - a) * fx) * (1.0 - fz) + (c + (d - c) * fx) * fz;
}

constexpr uint64_t SALT_CLEARING = 0x5EA501, SALT_AUTUMN = 0x5EA502, SALT_FLOWER = 0x5EA503,
                   SALT_FLOWER_KIND = 0x5EA504, SALT_LEAF = 0x5EA505;

} // namespace

namespace seasonal_forest {

// Open meadow where this is below CLEARING: no trees, flowers instead.
double clearing(uint64_t seed, int wx, int wz) {
    return patchNoise(seed, wx, wz, 24, SALT_CLEARING);
}

// How far a stand has turned (0 = all green, 1 = mostly red/orange).
double autumn(uint64_t seed, int wx, int wz) {
    return patchNoise(seed, wx, wz, 20, SALT_AUTUMN);
}

} // namespace seasonal_forest

namespace {

enum class Crown : uint8_t { Green, Red, Orange, RedOrange, Turning };

// Minecraft's small oak, with the crown's colours chosen per leaf from the
// world position (so a crown cut by a chunk border matches on both sides).
// `wide` grows the larger variant: taller trunk, radius-3 skirt.
bool deciduous(const TreeSite& s, Crown crown, bool wide) {
    const int trunkH = (wide ? 5 : 4) + static_cast<int>((s.bits >> 8) % 3); // 4..6 / 5..7
    const int top = s.surface + trunkH;
    if (!s.fits(top + 2)) return false;
    // Claim only the trunk's neighbours: the floor under these airy crowns
    // stays grassy and flowered instead of bare.
    s.claim(1);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, WOOD);

    auto leafAt = [&](int dx, int y, int dz) {
        const uint64_t h = hashCell(s.seed, s.wx + dx, (s.wz + dz) ^ (y << 20), SALT_LEAF);
        const uint32_t r = static_cast<uint32_t>(h % 100);
        switch (crown) {
        case Crown::Green:
            return LEAVES;
        case Crown::Red:
            return r < 12 ? ORANGE_AUTUMN_LEAVES : RED_AUTUMN_LEAVES;
        case Crown::Orange:
            return r < 12 ? RED_AUTUMN_LEAVES : ORANGE_AUTUMN_LEAVES;
        case Crown::RedOrange:
            return r < 50 ? RED_AUTUMN_LEAVES : ORANGE_AUTUMN_LEAVES;
        case Crown::Turning:
            // Still green inside, turning at the top and the edges.
            return (y >= top || std::abs(dx) + std::abs(dz) >= 3) && r < 70 ? ORANGE_AUTUMN_LEAVES : LEAVES;
        }
        return LEAVES;
    };

    // Layers from top - 3 (or - 4 for the wide crown) up to top + 1:
    // radius 2 (3) below, 1 at the top; outer corners dropped at random.
    const int bottom = top - (wide ? 4 : 3);
    for (int y = bottom; y <= top + 1; y++) {
        const int fromTop = top + 1 - y;
        int r = fromTop <= 1 ? 1 : 2;
        if (wide && y <= bottom + 1) r = 3;
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                const bool corner = std::abs(dx) == r && std::abs(dz) == r;
                if (corner) {
                    // Radius-3 rings lose their corners; others keep half.
                    if (r == 3 || fromTop == 0) continue;
                    const uint64_t h = hashCell(s.seed, s.wx + dx, (s.wz + dz) ^ (y << 20), SALT_LEAF + 1);
                    if (h & 1U) continue;
                }
                if (r == 3 && std::abs(dx) + std::abs(dz) > 4) continue;
                if (dx == 0 && dz == 0 && y <= top) continue; // trunk
                s.leaf(dx, y, dz, leafAt(dx, y, dz));
            }
    }
    return true;
}

// Flowers in patches: meadows (the clearings) and flower drifts elsewhere.
// Each drift is mostly one kind, like Minecraft's flower patches; the
// palette is warm (yellow, red, orange) to go with the turning leaves.
block_type flowerKind(uint64_t seed, int wx, int wz, uint32_t roll) {
    static constexpr block_type KINDS[] = {DANDELION, POPPY, ORANGE_TULIP};
    // One in five flowers in a drift is a stray of another kind.
    if (roll % 5 == 0) return KINDS[(roll / 5) % 3];
    const double kind = patchNoise(seed, wx, wz, 10, SALT_FLOWER_KIND);
    return kind < 0.36 ? DANDELION : kind < 0.62 ? POPPY : ORANGE_TULIP;
}

} // namespace

namespace seasonal_forest {

bool growTree(const TreeSite& s) {
    // Clearings stay open; their edges thin out instead of stopping dead.
    const double open = clearing(s.seed, s.wx, s.wz);
    if (open < CLEARING) return false;
    const uint32_t roll = static_cast<uint32_t>((s.bits >> 24) % 100);
    if (open < CLEARING + 0.08 && roll < 60) return false;

    const uint32_t kind = static_cast<uint32_t>((s.bits >> 32) % 100);
    if (kind < 22) return trees::birch(s);

    // Autumn stands: the patch value sets how many crowns have turned.
    const double turned = autumn(s.seed, s.wx, s.wz);
    const double pTurned = turned < 0.35 ? 0.12 : turned < 0.6 ? 0.40 : 0.75;
    const bool wide = ((s.bits >> 40) % 4) == 0;
    const double pick = static_cast<double>((s.bits >> 4) % 1000) / 1000.0;
    if (pick >= pTurned) return deciduous(s, Crown::Green, wide);
    const uint32_t shade = static_cast<uint32_t>((s.bits >> 16) % 100);
    const Crown crown = shade < 35   ? Crown::Orange
                        : shade < 65 ? Crown::Red
                        : shade < 85 ? Crown::RedOrange
                                     : Crown::Turning;
    return deciduous(s, crown, wide);
}

// Low leaf bushes in the understorey (green, sometimes turned), one column
// of log with a small leaf mound. Kept a block inside the chunk edge.
void decorate(const FeatureSite& f) {
    if (f.ground != GRASS || f.surface <= f.seaLevel + 1) return;
    if (f.i < 1 || f.i > CHUNK_SIZE - 2 || f.k < 1 || f.k > CHUNK_SIZE - 2) return;
    if (f.surface + 3 >= CHUNK_HEIGHT) return;
    if (f.bits % 100 >= 14) return;
    if (f.at(f.surface + 1) != AIR) return;
    const block_type leaves = (f.bits >> 8) % 3 == 0 ? ORANGE_AUTUMN_LEAVES : LEAVES;
    const int y = f.surface + 1;
    f.replace(0, y, 0, WOOD);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            if (f.biomeAt(dx, dz) != BIOME_SEASONAL_FOREST) continue; // stay in this biome
            // Only on solid ground at the same height (no floating leaves).
            if (f.atNear(dx, y - 1, dz) == AIR || f.atNear(dx, y - 1, dz) == WATER) continue;
            if (std::abs(dx) + std::abs(dz) == 2 && ((f.bits >> (12 + dx * 3 + dz + 4)) & 1U)) continue;
            f.setAt(dx, y, dz, leaves);
        }
    f.set(y + 1, leaves);
    f.claim();
}

block_type groundCover(const GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    const double open = clearing(g.seed, g.wx, g.wz);
    const double drift = patchNoise(g.seed, g.wx, g.wz, 12, SALT_FLOWER);
    // Flower chance per column (out of 10000): rich in clearings and in
    // drifts, a few strays under the trees.
    uint32_t flowers = 60;
    if (open < CLEARING) flowers = 1200;
    if (drift > 0.68) flowers += static_cast<uint32_t>((drift - 0.68) * 9000.0); // up to ~2900
    if (g.roll < flowers) return flowerKind(g.seed, g.wx, g.wz, g.roll);
    // Grass: thicker in the meadows.
    const uint32_t grass = open < CLEARING ? 2600 : 1600;
    if (g.roll < flowers + grass) return TALL_GRASS;
    return AIR;
}

} // namespace seasonal_forest

const BiomeDef SEASONAL_FOREST_DEF = {
    "Seasonal Forest",
    GRASS, DIRT, SAND,
    0.45f, &seasonal_forest::growTree,
    &seasonal_forest::decorate,
    &seasonal_forest::groundCover,
    // Warmer, yellower than the Forest: halfway between the jungle's
    // deep green and the savanna's straw.
    {1.00f, 1.03f, 0.80f}, // grass tint
    {0.92f, 1.03f, 0.62f}, // foliage tint (green oaks)
};
