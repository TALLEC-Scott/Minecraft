// 3D terrain density (terrain_density.h): overhangs and cuts around the
// heightmap in jagged land, untouched plains / coasts / rivers, no floating
// specks, and chunk-independent results. GL-free.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "TerrainGenerator.h"
#include "terrain_density.h"
#include "terrain_fill.h"

using terraindensity::ColumnBand;

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr int SEA = CHUNK_HEIGHT / 2;

// Jagged seasonal-forest mountains at seed 42 (found by scanning for
// high-relief columns): x ≈ -1260, z ≈ 440.
constexpr int MOUNTAIN_CX = -79, MOUNTAIN_CZ = 27;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int cx, int cz) { fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes); }
    block_type at(int x, int y, int z) const {
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// Shaped bands of the n × n columns at world (wx0, wz0).
std::vector<ColumnBand> shape(TerrainGenerator& tg, int wx0, int wz0, int n) {
    std::vector<int> h(static_cast<size_t>(n) * n);
    std::vector<float> j(h.size());
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) {
            TerrainGenerator::ColumnShape s = tg.getShape(wx0 + i, wz0 + k);
            h[i * n + k] = s.height;
            j[i * n + k] = s.jagged;
        }
    std::vector<ColumnBand> out(h.size());
    terraindensity::shapeColumns(tg, wx0, wz0, n, h.data(), j.data(), out.data());
    return out;
}

bool isGround(block_type t) {
    switch (t) {
    case STONE:
    case DIRT:
    case GRASS:
    case SNOW:
    case SAND:
    case GRAVEL:
    case COAL_ORE:
    case IRON_ORE:
    case DIAMOND_ORE:
    case BEDROCK:
        return true;
    default:
        return false;
    }
}

class TerrainDensityTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
};

} // namespace

TEST_F(TerrainDensityTest, DeterministicAndIndependentOfTheWindow) {
    // The same columns shaped as part of different squares (different
    // chunks asking) come out identical, and identical across generators.
    const int wx = MOUNTAIN_CX * CHUNK_SIZE, wz = MOUNTAIN_CZ * CHUNK_SIZE;
    std::vector<ColumnBand> a = shape(terrain, wx, wz, 16);
    std::vector<ColumnBand> b = shape(terrain, wx - 9, wz - 13, 40);
    TerrainGenerator other(42, 0.1f, 0, CHUNK_HEIGHT);
    std::vector<ColumnBand> c = shape(other, wx, wz, 16);
    int banded = 0;
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 16; k++) {
            const ColumnBand& x = a[i * 16 + k];
            const ColumnBand& y = b[(i + 9) * 40 + (k + 13)];
            const ColumnBand& z = c[i * 16 + k];
            if (x.count > 0) banded++;
            for (int h = 0; h < CHUNK_HEIGHT; h++) {
                ASSERT_EQ(x.solidAt(h), y.solidAt(h)) << i << "," << h << "," << k;
                ASSERT_EQ(x.solidAt(h), z.solidAt(h));
            }
            EXPECT_EQ(x.top, y.top);
            EXPECT_EQ(x.floor, y.floor);
        }
    EXPECT_GT(banded, 128) << "test area should be jagged";
}

TEST_F(TerrainDensityTest, ChunkHeightsAreTheRealTopAndMatchSurfaceHeights) {
    for (int dx = 0; dx < 2; dx++) {
        GenChunk c(terrain, MOUNTAIN_CX + dx, MOUNTAIN_CZ);
        int heights[CHUNK_SIZE][CHUNK_SIZE];
        terraindensity::surfaceHeights(terrain, MOUNTAIN_CX + dx, MOUNTAIN_CZ, heights);
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                const int h = c.heights[x][z];
                EXPECT_EQ(h, heights[x][z]);
                // Nothing of the ground above the reported top (trees and
                // plants may stand on it).
                for (int y = h + 1; y < CHUNK_HEIGHT; y++) ASSERT_FALSE(isGround(c.at(x, y, z))) << x << "," << y;
            }
    }
}

TEST_F(TerrainDensityTest, MountainsGetOverhangs) {
    // Some column has ground above air above ground within the band.
    int overhangs = 0;
    for (int dx = 0; dx < 3; dx++)
        for (int dz = 0; dz < 3; dz++) {
            GenChunk c(terrain, MOUNTAIN_CX - 1 + dx, MOUNTAIN_CZ - 1 + dz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    int y = h;
                    while (y > SEA && isGround(c.at(x, y, z))) y--;
                    const int gapTop = y;
                    while (y > SEA && c.at(x, y, z) == AIR) y--;
                    // Ground again within the density band: a real overhang,
                    // not a cave far below.
                    if (gapTop < h && y < gapTop && h - y <= 2 * terraindensity::AMPLITUDE && isGround(c.at(x, y, z)))
                        overhangs++;
                }
        }
    EXPECT_GT(overhangs, 40);
}

TEST_F(TerrainDensityTest, SurfaceBlocksSitOnTheRealTop) {
    // The top of an overhang is grass / snow / rock like any surface, and
    // the ground under it gets a surface of its own (not bare stone with
    // dirt floating above).
    int checked = 0;
    for (int dx = 0; dx < 2; dx++) {
        GenChunk c(terrain, MOUNTAIN_CX + dx, MOUNTAIN_CZ);
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                const int h = c.heights[x][z];
                const block_type top = c.at(x, h, z);
                if (top == AIR || top == LAVA || h <= SEA + 1) continue; // cave mouth / shore
                checked++;
                EXPECT_TRUE(top == GRASS || top == SNOW || top == STONE || top == DIRT || top == SAND ||
                            top == GRAVEL || top == COAL_ORE || top == IRON_ORE)
                    << "top " << top << " at " << x << "," << h << "," << z;
            }
    }
    EXPECT_GT(checked, 400);
}

TEST_F(TerrainDensityTest, NoFloatingSpecks) {
    // Shape a wide window and flood the ground from below: every solid block
    // of the inner square belongs to the grounded mass or to a piece too big
    // to be a speck.
    const int n = 64, inner0 = 16, inner1 = 48;
    const int wx = MOUNTAIN_CX * CHUNK_SIZE - 24, wz = MOUNTAIN_CZ * CHUNK_SIZE - 24;
    std::vector<ColumnBand> b = shape(terrain, wx, wz, n);
    const int y0 = SEA, y1 = terraindensity::CEILING + 1;
    const int H = y1 - y0 + 1;
    auto idx = [&](int i, int k, int y) { return (static_cast<size_t>(i) * n + k) * H + (y - y0); };
    std::vector<uint8_t> seen(static_cast<size_t>(n) * n * H, 0);
    std::vector<size_t> stack;
    auto solid = [&](int i, int k, int y) { return b[i * n + k].solidAt(y); };
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++)
            if (solid(i, k, y0)) {
                seen[idx(i, k, y0)] = 1;
                stack.push_back(idx(i, k, y0));
            }
    // Returns the horizontal footprint of everything reached.
    auto flood = [&](int& minI, int& maxI, int& minK, int& maxK) {
        while (!stack.empty()) {
            const size_t id = stack.back();
            stack.pop_back();
            const int y = static_cast<int>(id % H) + y0;
            const int col = static_cast<int>(id / H);
            const int i = col / n, k = col % n;
            minI = std::min(minI, i);
            maxI = std::max(maxI, i);
            minK = std::min(minK, k);
            maxK = std::max(maxK, k);
            const int nb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const auto& d : nb) {
                const int ni = i + d[0], ny = y + d[1], nk = k + d[2];
                if (ni < 0 || ni >= n || nk < 0 || nk >= n || ny < y0 || ny > y1) continue;
                if (seen[idx(ni, nk, ny)] || !solid(ni, nk, ny)) continue;
                seen[idx(ni, nk, ny)] = 1;
                stack.push_back(idx(ni, nk, ny));
            }
        }
    };
    int a = n, bb = -1, c = n, d = -1;
    flood(a, bb, c, d);
    int pieces = 0;
    for (int i = inner0; i < inner1; i++)
        for (int k = inner0; k < inner1; k++)
            for (int y = y0; y <= y1; y++) {
                if (seen[idx(i, k, y)] || !solid(i, k, y)) continue;
                seen[idx(i, k, y)] = 1;
                stack.push_back(idx(i, k, y));
                int minI = n, maxI = -1, minK = n, maxK = -1;
                flood(minI, maxI, minK, maxK);
                pieces++;
                EXPECT_FALSE(maxI - minI < terraindensity::FLOAT_MAX && maxK - minK < terraindensity::FLOAT_MAX)
                    << "floating speck near (" << wx + i << ", " << y << ", " << wz + k << ")";
            }
    EXPECT_LE(pieces, 2) << "floating islands should be rare";
}

TEST_F(TerrainDensityTest, FlatAndWetLandIsUntouched) {
    // Wherever jaggedness is 0 the ground is exactly the heightmap; and it is
    // 0 on ocean, beaches, river channels and low land.
    int plainCols = 0, plainsBiomeJagged = 0, plainsBiome = 0;
    for (int x = -1024; x < 1024; x += 7)
        for (int z = -1024; z < 1024; z += 7) {
            Biome b{};
            float jag = 0.0f;
            const int h = terrain.getHeightAndBiome(x, z, b, jag);
            EXPECT_FLOAT_EQ(jag, terrain.getShape(x, z).jagged);
            if (b == BIOME_OCEAN || b == BIOME_BEACH || h <= SEA + 4) {
                EXPECT_EQ(jag, 0.0f) << x << "," << z;
            }
            if (b == BIOME_PLAINS) {
                plainsBiome++;
                if (jag > 0.5f) plainsBiomeJagged++;
            }
        }
    // Only the odd hilly escarpment in plains gets real overhangs.
    EXPECT_LT(plainsBiomeJagged * 7, plainsBiome) << "plains should be almost entirely flat";

    // Spawn area (plains / forest): chunks with no jagged column generate
    // exactly on the heightmap.
    for (int cx = -3; cx < 3; cx++)
        for (int cz = -3; cz < 3; cz++) {
            bool any = false;
            for (int x = 0; x < CHUNK_SIZE && !any; x++)
                for (int z = 0; z < CHUNK_SIZE && !any; z++)
                    any = terrain.getShape(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z).jagged > 0.0f;
            if (any) continue;
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    plainCols++;
                    EXPECT_EQ(c.heights[x][z], terrain.getHeight(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z));
                }
        }
    EXPECT_GT(plainCols, 1000);
}

TEST_F(TerrainDensityTest, ExposedRockShowsCoal) {
    // Cliff faces, bare peaks and overhang undersides are stone with ore in
    // it, not plain stone: count rock blocks touching air above the water.
    long rock = 0, coal = 0;
    for (int dx = 0; dx < 3; dx++)
        for (int dz = 0; dz < 3; dz++) {
            GenChunk c(terrain, MOUNTAIN_CX - 1 + dx, MOUNTAIN_CZ - 1 + dz);
            for (int x = 1; x < CHUNK_SIZE - 1; x++)
                for (int z = 1; z < CHUNK_SIZE - 1; z++)
                    for (int y = SEA + 4; y < CHUNK_HEIGHT - 1; y++) {
                        const block_type t = c.at(x, y, z);
                        if (t != STONE && t != COAL_ORE && t != IRON_ORE) continue;
                        const bool exposed = c.at(x + 1, y, z) == AIR || c.at(x - 1, y, z) == AIR ||
                                             c.at(x, y + 1, z) == AIR || c.at(x, y - 1, z) == AIR ||
                                             c.at(x, y, z + 1) == AIR || c.at(x, y, z - 1) == AIR;
                        if (!exposed) continue;
                        rock++;
                        if (t == COAL_ORE) coal++;
                    }
        }
    ASSERT_GT(rock, 1000);
    EXPECT_GT(coal, 0);
    EXPECT_GT(static_cast<double>(coal) / rock, 0.002) << coal << " coal of " << rock << " exposed rock";
}
