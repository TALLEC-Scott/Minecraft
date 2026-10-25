#include "terrain_fill.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

using namespace terrainfill;

namespace {

// Margin of extra columns sampled around the chunk: tree canopy radius
// (3) + the footprint ring (1) + one more for the slope of those columns.
constexpr int MARGIN = 5;
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
void classify(Column& c, int sea, const BiomeParams& bp) {
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
        c.surface = bp.surfaceBlock;
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
        c.subsurface = DIRT; // under grass and snow
        break;
    }
}

} // namespace

void fillChunkTerrain(TerrainGenerator& terrain, int chunkX, int chunkZ, Cube* blocks,
                      int (&heights)[CHUNK_SIZE][CHUNK_SIZE], Biome (&biomes)[CHUNK_SIZE][CHUNK_SIZE]) {
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
            classify(c, sea, terrain.getBiomeParams(c.biome));
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
                    b.setType(terrain.isCoalOre(gx, j, gz) ? COAL_ORE : STONE);
                }
            }
        }
    }

    // --- Shore pass: land touching water at or near sea level → sand/gravel ---
    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            block_type shore = (biomes[i][k] == BIOME_TUNDRA) ? GRAVEL : SAND;
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

    // --- Vegetation ---
    // `claimed` marks in-chunk columns under a canopy (or holding a cactus /
    // plant) so ground cover never grows in the shade or overlaps.
    bool claimed[CHUNK_SIZE][CHUNK_SIZE] = {};
    auto inChunk = [](int i, int k) { return i >= 0 && i < CHUNK_SIZE && k >= 0 && k < CHUNK_SIZE; };
    auto setIfAir = [&](int i, int j, int k, block_type t) {
        if (!inChunk(i, k) || j < 0 || j >= CHUNK_HEIGHT) return;
        Cube& b = blocks[blockIndex(i, j, k)];
        if (b.getType() == AIR) b.setType(t);
    };

    // Trees: one candidate per TREE_CELL² world cell, at a hashed offset
    // (0..2 inside the cell, so trunks are ≥ 2 apart). Every chunk walks the
    // same cells in the same order and writes only its own blocks, so a
    // canopy that crosses a border is identical on both sides.
    constexpr int MAX_RADIUS = 3;
    const int reach = MAX_RADIUS + 1;
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
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
            float density = terrain.getBiomeParams(tc.biome).treeDensity;
            if (density <= 0.0f || unitFloat(hash4(seed, wx, wz, SALT_TREE + 100)) >= density) continue;
            // Decide from column data only (never from blocks a neighbor chunk
            // may have changed), so both sides of a border agree. sea + 1 is
            // excluded: the shore pass turns those columns to sand.
            if (tc.height <= sea + 1) continue;
            if (tc.surface != GRASS && tc.surface != SNOW) continue;
            // Flat-enough ground under the trunk.
            bool steep = false;
            for (int dx = -1; dx <= 1 && !steep; dx++)
                for (int dz = -1; dz <= 1 && !steep; dz++)
                    if (std::abs(col(ti + dx, tk + dz).height - tc.height) > 2) steep = true;
            if (steep) continue;
            // Don't plant on a cave mouth.
            if (terrain.caveEntranceAllowed(wx, wz) && terrain.isCave(wx, tc.height, wz)) continue;

            int trunkH = 4 + static_cast<int>((hsh >> 8) % 4);   // 4..7
            int radius = 2 + static_cast<int>((hsh >> 12) % 2);  // 2..3
            int layers = 3 + static_cast<int>((hsh >> 16) % 3);  // 3..5
            int surface = tc.height;
            if (surface + trunkH + layers + 2 >= CHUNK_HEIGHT) continue;

            for (int dx = -radius - 1; dx <= radius + 1; dx++)
                for (int dz = -radius - 1; dz <= radius + 1; dz++)
                    if (inChunk(ti + dx, tk + dz)) claimed[ti + dx][tk + dz] = true;

            // Trunk overwrites leaves from neighboring trees (order-independent).
            if (inChunk(ti, tk))
                for (int y = surface + 1; y <= surface + trunkH; y++) blocks[blockIndex(ti, y, tk)].setType(WOOD);

            int canopyBase = surface + trunkH;
            for (int ly = 0; ly < layers; ly++) {
                int layerR = std::max(1, radius - ly / 2);
                for (int dx = -layerR; dx <= layerR; dx++)
                    for (int dz = -layerR; dz <= layerR; dz++) {
                        if (std::abs(dx) == layerR && std::abs(dz) == layerR && layerR > 1) continue;
                        if (dx == 0 && dz == 0 && ly == 0) continue;
                        setIfAir(ti + dx, canopyBase + ly, tk + dz, LEAVES);
                    }
            }
            for (int dx = -1; dx <= 1; dx++)
                for (int dz = -1; dz <= 1; dz++)
                    if (std::abs(dx) + std::abs(dz) <= 1) setIfAir(ti + dx, canopyBase + layers, tk + dz, LEAVES);
        }
    }

    // Cacti: desert only, one candidate per tree cell, inside this chunk.
    for (int cz = floorDiv(oz, TREE_CELL); cz <= floorDiv(oz + CHUNK_SIZE - 1, TREE_CELL); cz++) {
        for (int cx = floorDiv(ox, TREE_CELL); cx <= floorDiv(ox + CHUNK_SIZE - 1, TREE_CELL); cx++) {
            uint64_t hsh = hash4(seed, cx, cz, SALT_CACTUS);
            int ci = cx * TREE_CELL + static_cast<int>(hsh % 3) - ox;
            int ck = cz * TREE_CELL + static_cast<int>((hsh >> 4) % 3) - oz;
            if (!inChunk(ci, ck) || claimed[ci][ck]) continue;
            if (unitFloat(hash4(seed, cx, cz, SALT_CACTUS + 100)) >= 0.12) continue;
            const Column& c = col(ci, ck);
            if (c.biome != BIOME_DESERT || c.height <= sea || c.surface != SAND) continue;
            if (blocks[blockIndex(ci, c.height, ck)].getType() != SAND) continue; // shore / cave
            int cactusH = 2 + static_cast<int>((hsh >> 8) % 3);
            if (c.height + cactusH >= CHUNK_HEIGHT) continue;
            for (int y = c.height + 1; y <= c.height + cactusH; y++) setIfAir(ci, y, ck, CACTUS);
            claimed[ci][ck] = true;
        }
    }

    // Ground cover: per-column roll on open grass.
    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            if (claimed[i][k]) continue;
            const Column& c = col(i, k);
            if (c.height <= sea || c.height + 1 >= CHUNK_HEIGHT) continue;
            if (blocks[blockIndex(i, c.height, k)].getType() != GRASS) continue;
            if (blocks[blockIndex(i, c.height + 1, k)].getType() != AIR) continue;
            int roll = static_cast<int>(hash4(seed, ox + i, oz + k, SALT_PLANT) % 100);
            block_type pick = AIR;
            switch (c.biome) {
            case BIOME_PLAINS:
                pick = roll < 10 ? TALL_GRASS : roll < 11 ? DANDELION : roll < 12 ? POPPY : AIR;
                break;
            case BIOME_FOREST:
                pick = roll < 8 ? TALL_GRASS : roll < 9 ? POPPY : AIR;
                break;
            case BIOME_TUNDRA:
            case BIOME_BEACH:
                pick = roll < 2 ? TALL_GRASS : AIR;
                break;
            default:
                break;
            }
            if (pick != AIR) blocks[blockIndex(i, c.height + 1, k)].setType(pick);
        }
    }
}
