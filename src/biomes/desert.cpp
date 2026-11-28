// Desert: open sand with cacti and dead bushes, and a sandstone layer a few
// blocks down (as in Minecraft), which shows in caves, ravines and pits.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

constexpr int CELL = 4;       // terrain_fill's TREE_CELL: one FeatureSite per cell
constexpr int SAND_DEPTH = 3; // loose sand on top; sandstone below
constexpr int SCAN = 10;      // how far above / below the site to look for a nearby column's top

int floorMod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// Top block of a nearby column: the first block from above that isn't air,
// water or a plant. -1 if none within SCAN of the site's surface.
int topOf(const biomegen::FeatureSite& f, int di, int dk) {
    const int hi = f.surface + SCAN < CHUNK_HEIGHT ? f.surface + SCAN : CHUNK_HEIGHT - 1;
    const int lo = f.surface - SCAN > 1 ? f.surface - SCAN : 1;
    for (int y = hi; y >= lo; y--) {
        const block_type t = f.atNear(di, y, dk);
        if (t != AIR && t != WATER && t != CACTUS && !hasFlag(t, BF_CROSS)) return y;
    }
    return -1;
}

// Sandstone under the sand for every desert column of this site's cell (the
// cells tile the chunk, so this covers all of the desert once): SAND_DEPTH
// blocks of sand, then the rest of the sand and 2..3 blocks of the stone
// below it turn to sandstone.
void sandstoneLayer(const biomegen::FeatureSite& f) {
    const int cellI = f.i - floorMod(f.wx, CELL), cellK = f.k - floorMod(f.wz, CELL);
    const int extra = 2 + static_cast<int>((f.bits >> 40) & 1);
    for (int ci = cellI; ci < cellI + CELL; ci++) {
        for (int ck = cellK; ck < cellK + CELL; ck++) {
            const int di = ci - f.i, dk = ck - f.k;
            if (f.biomeAt(di, dk) != BIOME_DESERT) continue;
            const int top = topOf(f, di, dk);
            if (top < 0 || f.atNear(di, top, dk) != SAND) continue;
            int y = top - SAND_DEPTH;
            for (; y > 0 && f.atNear(di, y, dk) == SAND; y--) f.replace(di, y, dk, SANDSTONE);
            for (int n = 0; n < extra && y > 0 && f.atNear(di, y, dk) == STONE; n++, y--)
                f.replace(di, y, dk, SANDSTONE);
        }
    }
}

// Room for a cactus on (di, dk) at height y: sand below, air in it and on
// its four sides (so cacti never touch, as in Minecraft).
bool cactusFits(const biomegen::FeatureSite& f, int di, int y, int dk) {
    if (f.atNear(di, y - 1, dk) != SAND || f.atNear(di, y, dk) != AIR) return false;
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : nb)
        if (f.atNear(di + d[0], y, dk + d[1]) != AIR) return false;
    return true;
}

void growCactus(const biomegen::FeatureSite& f, int di, int y, int dk, int h) {
    for (int n = 0; n < h && y + n < CHUNK_HEIGHT; n++) f.setAt(di, y + n, dk, CACTUS);
}

// Cacti: 10% of cells, 1..3 tall (mostly 2-3); a quarter of them have a
// smaller companion two blocks away.
void cacti(const biomegen::FeatureSite& f) {
    if ((f.bits >> 20) % 100 >= 10) return;
    if (f.surface <= f.seaLevel || f.ground != SAND) return;
    const int y = f.surface + 1;
    if (y + 3 >= CHUNK_HEIGHT || !cactusFits(f, 0, y, 0)) return;
    const int roll = static_cast<int>((f.bits >> 8) % 10);
    growCactus(f, 0, y, 0, roll < 2 ? 1 : roll < 6 ? 2 : 3);
    f.claim();

    if ((f.bits >> 28) % 4 != 0) return;
    static const int OFFSETS[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    const int* o = OFFSETS[(f.bits >> 32) % 4];
    if (f.biomeAt(o[0], o[1]) != BIOME_DESERT) return;
    const int top = topOf(f, o[0], o[1]);
    if (top <= f.seaLevel || top + 3 >= CHUNK_HEIGHT || !cactusFits(f, o[0], top + 1, o[1])) return;
    growCactus(f, o[0], top + 1, o[1], 1 + static_cast<int>((f.bits >> 36) % 2));
}

void decorate(const biomegen::FeatureSite& f) {
    sandstoneLayer(f);
    cacti(f);
}

// Dead bushes: scattered singly over the sand, ~1 in 110 columns.
block_type groundCover(const biomegen::GroundSite& g) {
    if (g.ground == SAND) return g.roll < 90 ? DEAD_BUSH : AIR;
    // Grass only on the rare grass column (at a border), as the hot, dry
    // climate grows it: hardly any.
    return g.ground == GRASS ? biomegen::climateGrass(g) : AIR;
}

} // namespace

const BiomeDef DESERT_DEF = {
    "Desert",
    SAND, SAND, SAND,
    0.0f, nullptr,
    &decorate,
    &groundCover,
    // Only seen where grass from a neighbour blends in: match savanna's
    // (the most common neighbour) so the border fades smoothly.
    {1.32f, 0.97f, 0.95f}, // grass tint
    {1.46f, 0.96f, 0.89f}, // foliage tint
};
