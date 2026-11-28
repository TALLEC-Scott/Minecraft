// Tundra (Minecraft Beta's snowy tundra): snow-covered ground, frozen
// rivers, gravel shores, sparse spruce that gathers into loose groves, the
// odd frost-capped boulder and small ice spike. Also the snow caps of
// mountains anywhere (altitude cooling in pickBiome), so everything here
// must look right on a high peak too.

#include <cstdint>

#include "biome_defs.h"
#include "terrain_fill.h"
#include "tree_shapes.h"

namespace {

using biomegen::FeatureSite;
using biomegen::TreeSite;

uint64_t mix(uint64_t v) {
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

double unit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int floorMod(int a, int b) {
    return a - floorDiv(a, b) * b;
}

// Smooth 0..1 value noise over GROVE-sized cells: where it is high the
// spruce gather into groves, elsewhere the snowfield stays nearly open.
constexpr int GROVE = 24;

double corner(uint64_t seed, int cx, int cz) {
    return unit(mix(seed ^ 0x7E1D5A0ULL ^ (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) ^
                    static_cast<uint32_t>(cz)));
}

double groveDensity(uint64_t seed, int wx, int wz) {
    const int cx = floorDiv(wx, GROVE), cz = floorDiv(wz, GROVE);
    const double fx = static_cast<double>(wx - cx * GROVE) / GROVE;
    const double fz = static_cast<double>(wz - cz * GROVE) / GROVE;
    const double sx = fx * fx * (3.0 - 2.0 * fx), sz = fz * fz * (3.0 - 2.0 * fz);
    const double a = corner(seed, cx, cz), b = corner(seed, cx + 1, cz);
    const double c = corner(seed, cx, cz + 1), d = corner(seed, cx + 1, cz + 1);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}

// Stock spruce, thinned outside the groves. A tall one gets a snow cap on
// its tip, like Minecraft's snowy spruce.
bool growTree(const TreeSite& s) {
    const double g = groveDensity(s.seed, s.wx, s.wz);
    // Keep chance: loose groves where the noise is high, a lone tree here
    // and there on the open snowfield.
    const double keep = g > 0.65 ? 0.7 : g > 0.5 ? 0.3 : 0.08;
    if (unit(mix(s.bits ^ 0x51CEULL)) >= keep) return false;
    return trees::spruce(s);
}

// --- Features ---

// Freeze still water at sea level in the tundra columns of this feature's
// TREE_CELL² cell. Cells tile chunks exactly (16 = 4 × 4), so a cell never
// straddles a chunk border, and the ice edge follows the biome edge.
void freezeCell(const FeatureSite& s) {
    constexpr int CELL = terrainfill::TREE_CELL;
    const int di0 = -floorMod(s.wx, CELL), dk0 = -floorMod(s.wz, CELL);
    const int y = s.seaLevel;
    for (int di = di0; di < di0 + CELL; di++)
        for (int dk = dk0; dk < dk0 + CELL; dk++)
            if (s.biomeAt(di, dk) == BIOME_TUNDRA && s.atNear(di, y, dk) == WATER && s.atNear(di, y + 1, dk) == AIR)
                s.replace(di, y, dk, ICE);
}

// Flat, open snow around the site (±1), so a feature sits on level ground.
bool flatSnow(const FeatureSite& s) {
    if (s.ground != SNOW || s.surface <= s.seaLevel + 1) return false;
    for (int di = -1; di <= 1; di++)
        for (int dk = -1; dk <= 1; dk++) {
            if (s.atNear(di, s.surface, dk) != SNOW) return false;
            if (s.atNear(di, s.surface + 1, dk) != AIR) return false;
        }
    return true;
}

// A low, lumpy boulder: a 2-high core with a few 1-high shoulders, some
// of it dusted with snow on top.
void boulder(const FeatureSite& s, uint64_t bits) {
    const int y = s.surface + 1;
    auto rock = [&](uint64_t b) { return (b & 3) == 0 ? STONE : COBBLESTONE; };
    s.set(y, rock(bits));
    s.set(y + 1, rock(bits >> 2));
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int n = 0; n < 4; n++)
        if ((bits >> (4 + n)) & 1) {
            s.setAt(nb[n][0], y, nb[n][1], rock(bits >> (8 + 2 * n)));
            if ((bits >> (16 + n)) & 1) s.setAt(nb[n][0], y + 1, nb[n][1], SNOW);
        }
    if ((bits >> 20) & 1) s.set(y + 2, SNOW);
}

// A small ice spike: a 3..6 tall column on a broad, low base.
void iceSpike(const FeatureSite& s, uint64_t bits) {
    const int y = s.surface + 1;
    const int h = 3 + static_cast<int>(bits % 4);
    for (int j = 0; j < h; j++) s.set(y + j, ICE);
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int n = 0; n < 4; n++) {
        const int tall = 1 + static_cast<int>((bits >> (4 + 2 * n)) % (h > 4 ? 3 : 2));
        for (int j = 0; j < tall; j++) s.setAt(nb[n][0], y + j, nb[n][1], ICE);
    }
}

void decorate(const FeatureSite& s) {
    freezeCell(s);

    // Multi-column features keep their ±1 footprint inside the site's own
    // cell (offsets 1..2 of 0..3), so they're never cut at a chunk border.
    constexpr int CELL = terrainfill::TREE_CELL;
    const int ox = floorMod(s.wx, CELL), oz = floorMod(s.wz, CELL);
    if (ox < 1 || ox > CELL - 2 || oz < 1 || oz > CELL - 2) return;
    if (!flatSnow(s)) return;

    const uint32_t roll = static_cast<uint32_t>(s.bits % 1000);
    const uint64_t bits = mix(s.bits);
    if (roll < 9) {
        boulder(s, bits);
        s.claim();
    } else if (roll < 13) {
        iceSpike(s, bits);
        s.claim();
    }
}

} // namespace

const BiomeDef TUNDRA_DEF = {
    "Tundra",
    SNOW, DIRT, GRAVEL,
    0.20f, &growTree,
    &decorate,
    nullptr, // no plants poke through the snow
    // Hardly any grass shows here (it's all snow), but the colours blend
    // ~4 blocks into the neighbours: a colder, paler take on taiga's
    // blue-green (0.84, 0.90, 1.50), so grass mutes toward the snow line.
    {0.80f, 0.88f, 1.60f}, // grass tint
    {0.78f, 0.90f, 1.80f}, // foliage tint
};
