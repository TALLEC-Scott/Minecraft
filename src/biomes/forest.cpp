// Forest: the classic temperate woods every other biome is compared with.
// Dense oaks, birch growing in coherent groves (the old birch-forest biome
// folded in here) rather than speckled at random, now and then a big
// "fancy" oak with branches and a lumpy canopy, a few fallen logs in the
// clearings, and a shaded floor with sparse grass and the odd flower patch.

#include <algorithm>
#include <cstdlib>

#include "biome_defs.h"
#include "biomes/forest.h"

using biomegen::FeatureSite;
using biomegen::GroundSite;
using biomegen::TreeSite;

namespace {

uint64_t mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t cellHash(uint64_t seed, int cx, int cz, uint64_t salt) {
    return mix(seed ^ mix(static_cast<uint64_t>(static_cast<uint32_t>(cx)) ^
                          (static_cast<uint64_t>(static_cast<uint32_t>(cz)) << 32) ^ (salt * 0x632BE59BD9B4E019ULL)));
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr uint64_t SALT_GROVE = 0xF0, SALT_FLOWERS = 0xF1;

// Fancy oak: a 7..10 trunk holding three or four leaf clusters, the top one
// on the trunk and the others pushed out on short branches, so the crown is
// broad and uneven (Minecraft's big oak, kept inside TREE_MAX_RADIUS).
void cluster(const TreeSite& s, int cx, int baseY, int cz) {
    // Four layers: narrow, wide, wide, narrow; the wide ones are discs of
    // radius 3 with the corners rounded off.
    static constexpr int RADII[4] = {2, 3, 3, 2};
    for (int ly = 0; ly < 4; ly++) {
        const int r = RADII[ly];
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++)
                if (dx * dx + dz * dz <= r * r + 1) s.leaf(cx + dx, baseY + ly, cz + dz, LEAVES);
    }
}

// The stock oak / birch silhouettes (tree_shapes.cpp), but claiming only the
// trunk's neighbours instead of the whole canopy footprint: a forest floor
// is shady, not bare, so grass and flowers still grow between the trunks.
void leafRing(const TreeSite& s, int y, int r, bool skipCenter, block_type leaves) {
    for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (std::abs(dx) == r && std::abs(dz) == r && r > 1) continue;
            if (dx == 0 && dz == 0 && skipCenter) continue;
            s.leaf(dx, y, dz, leaves);
        }
}

bool blobTree(const TreeSite& s, int trunkH, int radius, block_type log, block_type leaves) {
    const int layers = 3 + static_cast<int>((s.bits >> 16) % 3); // 3..5
    if (!s.fits(s.surface + trunkH + layers + 2)) return false;
    s.claim(1);
    for (int y = s.surface + 1; y <= s.surface + trunkH; y++) s.log(0, y, 0, log);
    const int top = s.surface + trunkH;
    for (int ly = 0; ly < layers; ly++) leafRing(s, top + ly, std::max(1, radius - ly / 2), ly == 0, leaves);
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++)
            if (std::abs(dx) + std::abs(dz) <= 1) s.leaf(dx, top + layers, dz, leaves);
    return true;
}

bool oak(const TreeSite& s) {
    const int trunkH = 4 + static_cast<int>((s.bits >> 8) % 4);  // 4..7
    const int radius = 2 + static_cast<int>((s.bits >> 12) % 2); // 2..3
    return blobTree(s, trunkH, radius, WOOD, LEAVES);
}

bool birch(const TreeSite& s) {
    const int trunkH = 5 + static_cast<int>((s.bits >> 8) % 3); // 5..7
    return blobTree(s, trunkH, 2, BIRCH_LOG, BIRCH_LEAVES);
}

} // namespace

namespace forest {

bool inBirchGrove(uint64_t seed, int wx, int wz) {
    // One candidate grove per GROVE_CELL² cell: a disc with a hashed centre
    // and radius. Any of the 3×3 cells around may reach this column.
    const int gx = floorDiv(wx, GROVE_CELL), gz = floorDiv(wz, GROVE_CELL);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            const uint64_t h = cellHash(seed, gx + dx, gz + dz, SALT_GROVE);
            if (h % 100 >= GROVE_CHANCE) continue;
            const int cx = (gx + dx) * GROVE_CELL + static_cast<int>((h >> 8) % GROVE_CELL);
            const int cz = (gz + dz) * GROVE_CELL + static_cast<int>((h >> 16) % GROVE_CELL);
            const int r = 7 + static_cast<int>((h >> 24) % 6); // 7..12
            const int ddx = wx - cx, ddz = wz - cz;
            if (ddx * ddx + ddz * ddz <= r * r) return true;
        }
    return false;
}

bool fancyOak(const TreeSite& s) {
    const int trunkH = 7 + static_cast<int>((s.bits >> 8) % 4); // 7..10
    const int top = s.surface + trunkH;
    if (!s.fits(top + 3)) return false;
    s.claim(2);
    for (int y = s.surface + 1; y <= top; y++) s.log(0, y, 0, WOOD);

    // Side clusters: two or three, on different sides, offset 2 out and
    // based 2..4 below the trunk top. A branch climbs diagonally to each.
    static constexpr int DIRS[8][2] = {{2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2}, {2, -2}};
    const int count = 2 + static_cast<int>((s.bits >> 12) % 2);
    const int first = static_cast<int>((s.bits >> 14) % 8);
    for (int c = 0; c < count; c++) {
        const int* d = DIRS[(first + c * 3) % 8]; // spread around the trunk
        const int base = top - 2 - static_cast<int>((s.bits >> (18 + c * 2)) % 3);
        // Branch: from the trunk at base - 1 out to the cluster's centre.
        const int ax = d[0] / 2, az = d[1] / 2;
        s.log(ax, base, az, WOOD);
        s.log(d[0], base + 1, d[1], WOOD);
        cluster(s, d[0], base + 1, d[1]);
    }
    cluster(s, 0, top - 1, 0);
    s.leaf(0, top + 3, 0, LEAVES);
    return true;
}

bool growTree(const TreeSite& s) {
    const bool grove = inBirchGrove(s.seed, s.wx, s.wz);
    // TreeSite::bits holds 44 bits; the tree shapes read the low ones, so
    // remix them for the species roll.
    const auto roll = static_cast<uint32_t>(mix(s.bits ^ 0xF0E57ULL) % 100);
    // Groves are nearly all birch with the odd oak; elsewhere a rare
    // stray birch, and about one oak in twelve grows big.
    if (grove ? roll < 88 : roll < 3) return birch(s);
    if (!grove && roll >= 92) return fancyOak(s);
    return oak(s);
}

void decorate(const FeatureSite& f) {
    // Fallen oak logs in the clearings: 3..5 blocks lying on level grass,
    // along X or Z. Kept inside the chunk (a log would otherwise be cut in
    // half at the border), and only where every block rests on the ground.
    if (f.ground != GRASS || f.surface <= f.seaLevel) return;
    if (f.bits % 100 >= FALLEN_LOG_CHANCE) return;
    const bool alongX = ((f.bits >> 8) & 1) != 0;
    const int len = 3 + static_cast<int>((f.bits >> 9) % 3);
    const int start = -static_cast<int>((f.bits >> 12) % static_cast<uint64_t>(len)); // log covers start..start+len-1
    const int along = alongX ? f.i : f.k;
    if (along + start < 0 || along + start + len - 1 >= CHUNK_SIZE) return;
    for (int n = start; n < start + len; n++) {
        const int di = alongX ? n : 0, dk = alongX ? 0 : n;
        const block_type ground = f.atNear(di, f.surface, dk);
        if (ground != GRASS && ground != DIRT) return;
        if (f.atNear(di, f.surface + 1, dk) != AIR) return;
    }
    const block_type log = alongX ? OAK_LOG_X : OAK_LOG_Z;
    for (int n = start; n < start + len; n++) f.setAt(alongX ? n : 0, f.surface + 1, alongX ? 0 : n, log);
    f.claim();
}

block_type groundCover(const GroundSite& g) {
    if (g.ground != GRASS) return AIR;
    // Flower patches: a few 8×8 cells bloom, the rest keep to grass with a
    // rare flower, so the floor stays calm under the trees.
    const uint64_t patch = cellHash(g.seed, floorDiv(g.wx, 8), floorDiv(g.wz, 8), SALT_FLOWERS);
    if (patch % 100 < 12) {
        const block_type flower = ((patch >> 8) & 1) != 0 ? POPPY : DANDELION;
        if (g.roll < 900) return flower;
        return g.roll < 2500 ? TALL_GRASS : AIR;
    }
    if (g.roll < 1800) return TALL_GRASS;
    if (g.roll < 1830) return POPPY;
    if (g.roll < 1860) return DANDELION;
    return AIR;
}

} // namespace forest

const BiomeDef FOREST_DEF = {
    "Forest",
    GRASS, DIRT, SAND,
    0.60f, &forest::growTree,
    &forest::decorate,
    &forest::groundCover,
    {0.83f, 1.02f, 1.01f}, // grass tint
    {0.75f, 1.02f, 1.02f}, // foliage tint
};
