#include "terrain_fill.h"

#include <algorithm>

#include "biome.h"
#include "biome_tint.h"
#include "vine.h"
#include <cstdint>
#include <cstdlib>

using namespace terrainfill;

namespace {

// Margin of extra columns sampled around the chunk: the largest tree canopy
// radius + the footprint ring (1) + one more for the slope of those columns.
constexpr int MARGIN = biomegen::TREE_MAX_RADIUS + 2;
static_assert(MARGIN >= TINT_BLEND_RADIUS, "the colour blend reads biomes from the margin");
constexpr int EXT = CHUNK_SIZE + 2 * MARGIN;
constexpr int CAVE_FLOOR = 5; // never carve into the bottom layers
// Cave space at or below this height fills with lava instead of air
// (Minecraft's old "lava level"): deep caves glow and are dangerous.
constexpr int LAVA_LEVEL = 10;

inline size_t blockIndex(int x, int y, int z) {
    return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
}

// splitmix64 over (seed, x, z, salt): stable per-world-position randomness
// that doesn't depend on which chunk asks, unlike a per-chunk RNG stream.
uint64_t hash4(uint64_t seed, int x, int z, uint64_t salt) {
    uint64_t v = seed * 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^
                 static_cast<uint32_t>(z) ^ (salt * 0xD1B54A32D192ED03ULL);
    v += 0x9E3779B97F4A7C15ULL;
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

double unitFloat(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

constexpr uint64_t SALT_TREE = 1, SALT_CACTUS = 2, SALT_PLANT = 3;

struct Column {
    int height;
    Biome biome;
    int slope; // largest height step to a 4-neighbor
    block_type surface;
    block_type subsurface;
};

// Surface rules: water floor, snow line, cliffs and high rock override the
// biome's own surface block.
void classify(Column& c, int sea, const BiomeDef& bp) {
    int above = c.height - sea;
    // Bare rock: cliff faces anywhere, and moderate slopes in the high band
    // below the snow line.
    bool rock = c.slope >= CLIFF_SLOPE || (above >= ROCK_LINE && above < SNOW_LINE && c.slope >= 2);
    if (c.height < sea) {
        c.surface = c.height < sea - 10 ? GRAVEL : SAND; // deep ocean floor is gravel
    } else if (rock) {
        c.surface = STONE;
    } else if (above >= SNOW_LINE) {
        c.surface = SNOW;
    } else {
        c.surface = bp.surface;
    }
    switch (c.surface) {
    case STONE:
        c.subsurface = STONE;
        break;
    case SAND:
        c.subsurface = SAND;
        break;
    case GRAVEL:
        c.subsurface = GRAVEL;
        break;
    default:
        c.subsurface = bp.subsurface; // under the biome's own surface, and snow
        break;
    }
}

// What a BiomeDef hook writes through (biome.h TreeSite / FeatureSite).
struct GenContext {
    Cube* blocks;
    bool (&claimed)[CHUNK_SIZE][CHUNK_SIZE];
    const Column (*cols)[EXT]; // column grid incl. the MARGIN ring, indexed [i + MARGIN][k + MARGIN]
    uint8_t* states;           // per-cell state bytes (vine sides), may be null

    // Biome of chunk-local column (i, k), clamped to the sampled margin.
    Biome biome(int i, int k) const {
        i = std::clamp(i, -MARGIN, CHUNK_SIZE + MARGIN - 1);
        k = std::clamp(k, -MARGIN, CHUNK_SIZE + MARGIN - 1);
        return cols[i + MARGIN][k + MARGIN].biome;
    }

    static bool inChunk(int i, int k) { return i >= 0 && i < CHUNK_SIZE && k >= 0 && k < CHUNK_SIZE; }
    block_type get(int i, int j, int k) const {
        if (!inChunk(i, k) || j < 0 || j >= CHUNK_HEIGHT) return AIR;
        return blocks[blockIndex(i, j, k)].getType();
    }
    void set(int i, int j, int k, block_type t) const {
        if (!inChunk(i, k) || j < 0 || j >= CHUNK_HEIGHT) return;
        blocks[blockIndex(i, j, k)].setType(t);
        if (states) states[blockIndex(i, j, k)] = 0; // e.g. a trunk over a vine
    }
    int vine(int i, int j, int k, uint8_t sides, int len) const {
        if (!inChunk(i, k)) return 0;
        int n = 0;
        for (int y = j; y > j - len && y >= 1 && y < CHUNK_HEIGHT; y--) {
            const block_type here = blocks[blockIndex(i, y, k)].getType();
            const block_type below = blocks[blockIndex(i, y - 1, k)].getType();
            if ((here != AIR && here != VINE) || (below != AIR && below != VINE)) break;
            blocks[blockIndex(i, y, k)].setType(VINE);
            if (states) states[blockIndex(i, y, k)] |= sides;
            wroteVines = true;
            n++;
        }
        return n;
    }
    // After all trees: a vine side whose leaf never got written (its cell
    // was taken) and that doesn't continue the vine above loses that side;
    // a vine left with none goes. Sides facing another chunk are kept (the
    // tree there is drawn the same way from both sides).
    void pruneVines() const {
        for (int i = 0; i < CHUNK_SIZE; i++)
            for (int k = 0; k < CHUNK_SIZE; k++)
                for (int j = CHUNK_HEIGHT - 2; j >= 1; j--) {
                    const size_t at = blockIndex(i, j, k);
                    if (blocks[at].getType() != VINE) continue;
                    const bool vineAbove = blocks[blockIndex(i, j + 1, k)].getType() == VINE;
                    const uint8_t above = vineAbove ? states[blockIndex(i, j + 1, k)] : 0;
                    uint8_t keep = 0;
                    for (int b = 0; b < 4; b++) {
                        const uint8_t bit = static_cast<uint8_t>(1u << b);
                        if ((states[at] & bit) == 0) continue;
                        const int ni = i + vine::SIDE[b][0], nk = k + vine::SIDE[b][2];
                        if (!inChunk(ni, nk) || vine::canSupport(get(ni, j, nk)) || (above & bit) != 0) keep |= bit;
                    }
                    states[at] = keep;
                    if (keep == 0) blocks[at].setType(AIR);
                }
    }
    mutable bool wroteVines = false;
    void setIfAir(int i, int j, int k, block_type t) const {
        if (!inChunk(i, k) || j < 0 || j >= CHUNK_HEIGHT) return;
        Cube& b = blocks[blockIndex(i, j, k)];
        if (b.getType() == AIR) b.setType(t);
    }
};

} // namespace

void fillChunkTerrain(TerrainGenerator& terrain, int chunkX, int chunkZ, Cube* blocks,
                      int (&heights)[CHUNK_SIZE][CHUNK_SIZE], Biome (&biomes)[CHUNK_SIZE][CHUNK_SIZE],
                      BiomeTints* tints, uint8_t* states) {
    const int sea = terrain.getSeaLevel();
    const int ox = chunkX * CHUNK_SIZE, oz = chunkZ * CHUNK_SIZE;
    const uint64_t seed = terrain.getSeed();

    // --- Column grid with margin (local coords -MARGIN .. CHUNK_SIZE+MARGIN) ---
    static thread_local Column cols[EXT][EXT];
    auto col = [&](int i, int k) -> Column& { return cols[i + MARGIN][k + MARGIN]; };
    for (int i = -MARGIN; i < CHUNK_SIZE + MARGIN; i++)
        for (int k = -MARGIN; k < CHUNK_SIZE + MARGIN; k++) {
            Column& c = col(i, k);
            c.height = terrain.getHeightAndBiome(ox + i, oz + k, c.biome);
        }
    for (int i = -MARGIN; i < CHUNK_SIZE + MARGIN; i++)
        for (int k = -MARGIN; k < CHUNK_SIZE + MARGIN; k++) {
            Column& c = col(i, k);
            c.slope = 0;
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : nb) {
                int ni = i + d[0], nk = k + d[1];
                if (ni < -MARGIN || ni >= CHUNK_SIZE + MARGIN || nk < -MARGIN || nk >= CHUNK_SIZE + MARGIN) continue;
                c.slope = std::max(c.slope, std::abs(col(ni, nk).height - c.height));
            }
            classify(c, sea, biomeDef(c.biome));
        }

    // --- Terrain columns ---
    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            const Column& c = col(i, k);
            const int gx = ox + i, gz = oz + k;
            const int h = c.height;
            heights[i][k] = h;
            biomes[i][k] = c.biome;

            const int soil = terrain.soilDepth(gx, gz);
            // Caves keep a solid crust (or a thick roof under water), except
            // in rare entrance zones where tunnels may breach the surface.
            int caveCeil;
            if (h <= sea + 1)
                caveCeil = h - OCEAN_ROOF;
            else
                caveCeil = terrain.caveEntranceAllowed(gx, gz) ? h : h - CAVE_CRUST;

            const int top = std::max(h, sea);
            for (int j = 0; j <= top && j < CHUNK_HEIGHT; j++) {
                Cube& b = blocks[blockIndex(i, j, k)];
                if (j > h) {
                    b.setType(WATER); // j <= sea here
                } else if (j == 0) {
                    b.setType(BEDROCK);
                } else if (j >= CAVE_FLOOR && j <= caveCeil && terrain.isCave(gx, j, gz)) {
                    b.setType(j <= LAVA_LEVEL ? LAVA : AIR);
                } else if (j == h) {
                    b.setType(c.surface);
                } else if (j > h - soil) {
                    b.setType(c.subsurface);
                } else {
                    b.setType(terrain.oreAt(gx, j, gz));
                }
            }
        }
    }

    // --- Shore pass: land touching water at or near sea level → sand/gravel ---
    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            const block_type shore = biomeDef(biomes[i][k]).shore;
            for (int j = 1; j <= sea + 1 && j < CHUNK_HEIGHT; j++) {
                Cube& b = blocks[blockIndex(i, j, k)];
                block_type bt = b.getType();
                if (bt != DIRT && bt != GRASS && bt != STONE && bt != SNOW) continue;
                bool wet = false;
                const int dirs[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (auto& d : dirs) {
                    int ni = i + d[0], nj = j + d[1], nk = k + d[2];
                    if (ni < 0 || ni >= CHUNK_SIZE || nk < 0 || nk >= CHUNK_SIZE || nj >= CHUNK_HEIGHT) continue;
                    if (blocks[blockIndex(ni, nj, nk)].getType() == WATER) {
                        wet = true;
                        break;
                    }
                }
                if (wet) b.setType(shore);
            }
        }
    }

    if (tints) blendBiomeTints([&](int i, int k) { return col(i, k).biome; }, *tints);

    // --- Vegetation ---
    // `claimed` marks in-chunk columns under a canopy (or holding a feature
    // / plant) so ground cover never grows in the shade or overlaps.
    bool claimed[CHUNK_SIZE][CHUNK_SIZE] = {};
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    GenContext ctx{blocks, claimed, cols, states};

    // Trees: one candidate per TREE_CELL² world cell, at a hashed offset
    // (0..2 inside the cell, so trunks are ≥ 2 apart). Every chunk walks the
    // same cells in the same order and writes only its own blocks, so a
    // canopy that crosses a border is identical on both sides.
    const int reach = biomegen::TREE_MAX_RADIUS + 1;
    const int cx0 = floorDiv(ox - reach, TREE_CELL), cx1 = floorDiv(ox + CHUNK_SIZE - 1 + reach, TREE_CELL);
    const int cz0 = floorDiv(oz - reach, TREE_CELL), cz1 = floorDiv(oz + CHUNK_SIZE - 1 + reach, TREE_CELL);
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            uint64_t hsh = hash4(seed, cx, cz, SALT_TREE);
            int wx = cx * TREE_CELL + static_cast<int>(hsh % 3);
            int wz = cz * TREE_CELL + static_cast<int>((hsh >> 4) % 3);
            int ti = wx - ox, tk = wz - oz;
            if (ti < -reach || ti >= CHUNK_SIZE + reach || tk < -reach || tk >= CHUNK_SIZE + reach) continue;

            const Column& tc = col(ti, tk);
            const BiomeDef& def = biomeDef(tc.biome);
            if (!def.growTree || def.treeDensity <= 0.0f) continue;
            if (unitFloat(hash4(seed, wx, wz, SALT_TREE + 100)) >= def.treeDensity) continue;
            // Decide from column data only (never from blocks a neighbor chunk
            // may have changed), so both sides of a border agree. sea + 1 is
            // excluded: the shore pass turns those columns to sand.
            if (tc.height <= sea + 1) continue;
            if (tc.surface != def.surface && tc.surface != SNOW) continue;
            if (tc.surface == SAND || tc.surface == STONE || tc.surface == GRAVEL) continue;
            // Flat-enough ground under the trunk.
            bool steep = false;
            for (int dx = -1; dx <= 1 && !steep; dx++)
                for (int dz = -1; dz <= 1 && !steep; dz++)
                    if (std::abs(col(ti + dx, tk + dz).height - tc.height) > 2) steep = true;
            if (steep) continue;
            // Don't plant on a cave mouth.
            if (terrain.caveEntranceAllowed(wx, wz) && terrain.isCave(wx, tc.height, wz)) continue;

            biomegen::TreeSite site{ti, tk, tc.height, hsh >> 20, wx, wz, seed, &ctx};
            def.growTree(site);
        }
    }

    // Features (cactus...): one candidate per tree cell, inside this chunk,
    // at its own hashed offset; the biome decides whether anything grows.
    for (int cz = floorDiv(oz, TREE_CELL); cz <= floorDiv(oz + CHUNK_SIZE - 1, TREE_CELL); cz++) {
        for (int cx = floorDiv(ox, TREE_CELL); cx <= floorDiv(ox + CHUNK_SIZE - 1, TREE_CELL); cx++) {
            uint64_t hsh = hash4(seed, cx, cz, SALT_CACTUS);
            int ci = cx * TREE_CELL + static_cast<int>(hsh % 3) - ox;
            int ck = cz * TREE_CELL + static_cast<int>((hsh >> 4) % 3) - oz;
            if (!GenContext::inChunk(ci, ck) || claimed[ci][ck]) continue;
            const Column& c = col(ci, ck);
            const BiomeDef& def = biomeDef(c.biome);
            if (!def.decorate) continue;
            biomegen::FeatureSite site{ci,
                                       ck,
                                       c.height,
                                       blocks[blockIndex(ci, c.height, ck)].getType(),
                                       sea,
                                       hash4(seed, cx, cz, SALT_CACTUS + 100),
                                       ox + ci,
                                       oz + ck,
                                       seed,
                                       &ctx};
            def.decorate(site);
        }
    }

    if (states && ctx.wroteVines) ctx.pruneVines();

    // Ground cover: per-column roll on open ground above the water, not
    // right under a vine (vines keep a free cell under them).
    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            if (claimed[i][k]) continue;
            const Column& c = col(i, k);
            const BiomeDef& def = biomeDef(c.biome);
            if (!def.groundCover || c.height <= sea || c.height + 1 >= CHUNK_HEIGHT) continue;
            if (blocks[blockIndex(i, c.height + 1, k)].getType() != AIR) continue;
            if (c.height + 2 < CHUNK_HEIGHT && blocks[blockIndex(i, c.height + 2, k)].getType() == VINE) continue;
            const block_type ground = blocks[blockIndex(i, c.height, k)].getType();
            const auto roll = static_cast<uint32_t>(hash4(seed, ox + i, oz + k, SALT_PLANT) % 10000);
            const block_type pick = def.groundCover(biomegen::GroundSite{ground, roll, ox + i, oz + k, seed});
            if (pick != AIR) blocks[blockIndex(i, c.height + 1, k)].setType(pick);
        }
    }
}

// --- BiomeDef hook plumbing (biome.h) ---

namespace biomegen {

void TreeSite::leaf(int di, int y, int dk, block_type t) const {
    static_cast<GenContext*>(impl)->setIfAir(i + di, y, k + dk, t);
}
void TreeSite::log(int di, int y, int dk, block_type t) const {
    static_cast<GenContext*>(impl)->set(i + di, y, k + dk, t);
}
int TreeSite::vine(int di, int y, int dk, uint8_t sides, int len) const {
    return static_cast<GenContext*>(impl)->vine(i + di, y, k + dk, sides, len);
}
void TreeSite::claim(int radius) const {
    auto* ctx = static_cast<GenContext*>(impl);
    for (int dx = -radius; dx <= radius; dx++)
        for (int dz = -radius; dz <= radius; dz++)
            if (GenContext::inChunk(i + dx, k + dz)) ctx->claimed[i + dx][k + dz] = true;
}
bool TreeSite::fits(int top) const {
    return top < CHUNK_HEIGHT;
}

block_type FeatureSite::at(int y) const {
    return static_cast<GenContext*>(impl)->get(i, y, k);
}
block_type FeatureSite::atNear(int di, int y, int dk) const {
    return static_cast<GenContext*>(impl)->get(i + di, y, k + dk);
}
void FeatureSite::replace(int di, int y, int dk, block_type t) const {
    static_cast<GenContext*>(impl)->set(i + di, y, k + dk, t);
}
void FeatureSite::set(int y, block_type t) const {
    static_cast<GenContext*>(impl)->setIfAir(i, y, k, t);
}
void FeatureSite::setAt(int di, int y, int dk, block_type t) const {
    static_cast<GenContext*>(impl)->setIfAir(i + di, y, k + dk, t);
}
Biome FeatureSite::biomeAt(int di, int dk) const {
    return static_cast<GenContext*>(impl)->biome(i + di, k + dk);
}
void FeatureSite::claim() const {
    static_cast<GenContext*>(impl)->claimed[i][k] = true;
}

} // namespace biomegen
