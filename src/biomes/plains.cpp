// Plains: rolling open grassland. Tall grass (short and two-block) grows in
// drifts (thick swathes fading into short turf) over the climate's base, flowers bloom in meadow patches each dominated by
// one kind (dandelion, poppy, oxeye daisy, cornflower), and trees are rare: a
// lone oak here and there, or a small copse of two or three. Its colours are
// the stock textures' (tint 1,1,1), the reference the other biomes tint from.

#include "biomes/plains.h"

#include "biome_defs.h"
#include "tree_shapes.h"

#include <cmath>

namespace plains {

namespace {

uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t cellHash(uint64_t seed, int cx, int cz, uint64_t salt) {
    return mix(seed ^ mix(static_cast<uint64_t>(static_cast<uint32_t>(cx)) ^
                          (static_cast<uint64_t>(static_cast<uint32_t>(cz)) << 32) ^ salt));
}

double unit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr uint64_t SALT_GRASS = 0x6772617373ULL;  // "grass"
constexpr uint64_t SALT_MEADOW = 0x6D6561646FULL; // "meado"
constexpr uint64_t SALT_COPSE = 0x636F707365ULL;  // "copse"
constexpr uint64_t SALT_LONE = 0x6C6F6E65ULL;     // "lone"
constexpr uint64_t SALT_COLUMN = 0x636F6CULL;     // "col"

} // namespace

// Smooth 0..1 value noise over GRASS_CELL-block cells (bilinear between
// hashed corners, smoothstepped): where the tall grass is thick.
double grassDensity(int wx, int wz, uint64_t seed) {
    const int cx = floorDiv(wx, GRASS_CELL), cz = floorDiv(wz, GRASS_CELL);
    double fx = static_cast<double>(wx - cx * GRASS_CELL) / GRASS_CELL;
    double fz = static_cast<double>(wz - cz * GRASS_CELL) / GRASS_CELL;
    fx = fx * fx * (3.0 - 2.0 * fx);
    fz = fz * fz * (3.0 - 2.0 * fz);
    const double a = unit(cellHash(seed, cx, cz, SALT_GRASS));
    const double b = unit(cellHash(seed, cx + 1, cz, SALT_GRASS));
    const double c = unit(cellHash(seed, cx, cz + 1, SALT_GRASS));
    const double d = unit(cellHash(seed, cx + 1, cz + 1, SALT_GRASS));
    return (a + (b - a) * fx) * (1.0 - fz) + (c + (d - c) * fx) * fz;
}

// Meadow patches: each MEADOW_CELL² cell may hold one round patch (centre and
// radius hashed) of one dominant flower. Returns that flower and how far into
// the patch the column is (1 at the centre, 0 at the rim), or AIR / 0.
block_type meadowAt(int wx, int wz, uint64_t seed, double& strength) {
    static constexpr block_type FLOWERS[] = {DANDELION, DANDELION, POPPY, POPPY, OXEYE_DAISY, CORNFLOWER};
    strength = 0.0;
    block_type best = AIR;
    const int cx0 = floorDiv(wx, MEADOW_CELL), cz0 = floorDiv(wz, MEADOW_CELL);
    for (int cz = cz0 - 1; cz <= cz0 + 1; cz++)
        for (int cx = cx0 - 1; cx <= cx0 + 1; cx++) {
            const uint64_t h = cellHash(seed, cx, cz, SALT_MEADOW);
            if (h % 100 >= MEADOW_CHANCE) continue;
            const int px = cx * MEADOW_CELL + static_cast<int>((h >> 8) % MEADOW_CELL);
            const int pz = cz * MEADOW_CELL + static_cast<int>((h >> 16) % MEADOW_CELL);
            const double r = 3.0 + static_cast<double>((h >> 24) % 5); // 3..7
            const double dx = wx - px, dz = wz - pz;
            const double s = 1.0 - std::sqrt(dx * dx + dz * dz) / r;
            if (s > strength) {
                strength = s;
                best = FLOWERS[(h >> 32) % (sizeof(FLOWERS) / sizeof(FLOWERS[0]))];
            }
        }
    return best;
}

block_type groundCover(const biomegen::GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    const uint64_t h = cellHash(g.seed, g.wx, g.wz, SALT_COLUMN);
    const double r1 = unit(h), r2 = unit(mix(h));

    double strength = 0.0;
    const block_type flower = meadowAt(g.wx, g.wz, g.seed, strength);
    if (flower != AIR && r1 < 0.08 + 0.42 * strength) {
        // Mostly the patch's flower, a few strays of the commonest two.
        if (r2 < 0.82) return flower;
        return r2 < 0.91 ? DANDELION : POPPY;
    }
    // The odd lone flower anywhere.
    if (r1 > 0.996) return r2 < 0.6 ? DANDELION : POPPY;

    // Grass on the climate's base (biome.h climateGrass): sparse turf
    // (x0.3) up to thick drifts (x1.7), the drifts richest in two-block grass.
    const double n = grassDensity(g.wx, g.wz, g.seed);
    return biomegen::climateGrass(g, 0.3 + 1.4 * n * n, 0.3 + 1.0 * n);
}

// Trees are rare: a copse of a few oaks round some COPSE_CELL cells' hashed
// centre, otherwise a lone oak now and then.
bool growTree(const biomegen::TreeSite& s) {
    const int cx = floorDiv(s.wx, COPSE_CELL), cz = floorDiv(s.wz, COPSE_CELL);
    const uint64_t h = cellHash(s.seed, cx, cz, SALT_COPSE);
    bool grow = false;
    if (h % 100 < COPSE_CHANCE) {
        const int px = cx * COPSE_CELL + 6 + static_cast<int>((h >> 8) % (COPSE_CELL - 12));
        const int pz = cz * COPSE_CELL + 6 + static_cast<int>((h >> 16) % (COPSE_CELL - 12));
        const int dx = s.wx - px, dz = s.wz - pz;
        if (dx * dx + dz * dz <= COPSE_RADIUS * COPSE_RADIUS) grow = (s.bits >> 50) % 100 < 70;
    }
    if (!grow) grow = cellHash(s.seed, s.wx, s.wz, SALT_LONE) % 1000 < LONE_TREE_PER_MILLE;
    return grow && trees::oak(s);
}

} // namespace plains

// Every tree cell is offered to plains::growTree, which does its own (rare)
// copse / lone-tree selection.
const BiomeDef PLAINS_DEF = {
    "Plains",
    GRASS, DIRT, SAND,
    1.0f, &plains::growTree,
    nullptr,
    &plains::groundCover,
    {1.0f, 1.0f, 1.0f}, // grass tint
    {1.0f, 1.0f, 1.0f}, // foliage tint
};
