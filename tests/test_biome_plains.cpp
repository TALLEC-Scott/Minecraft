// Plains (src/biomes/plains.cpp): tall grass in drifts, flower meadows with
// the plains-only flowers, and few trees.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include "TerrainGenerator.h"
#include "biomes/plains.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr uint64_t SEED = 42;

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

biomegen::GroundSite grassAt(int wx, int wz, uint32_t roll = 0) {
    return biomegen::GroundSite{GRASS, roll, wx, wz, SEED};
}

} // namespace

TEST(PlainsTest, GrassDensityIsSmoothPatches) {
    double lo = 1.0, hi = 0.0;
    for (int z = 0; z < 200; z++)
        for (int x = 0; x < 200; x++) {
            const double d = plains::grassDensity(x, z, SEED);
            ASSERT_GE(d, 0.0);
            ASSERT_LE(d, 1.0);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
            // Neighbouring columns differ only a little: drifts, not speckle.
            EXPECT_LT(std::abs(d - plains::grassDensity(x + 1, z, SEED)), 0.35);
        }
    EXPECT_LT(lo, 0.15) << "some open turf";
    EXPECT_GT(hi, 0.85) << "some thick drifts";
}

TEST(PlainsTest, GroundCoverMixAndOnlyOnGrass) {
    std::map<block_type, int> n;
    const int side = 256;
    for (int z = 0; z < side; z++)
        for (int x = 0; x < side; x++) n[plains::groundCover(grassAt(x, z))]++;
    const double total = side * side;
    const double grass = n[TALL_GRASS] / total;
    const double flowers = (n[DANDELION] + n[POPPY] + n[OXEYE_DAISY] + n[CORNFLOWER]) / total;
    EXPECT_GT(grass, 0.10);
    EXPECT_LT(grass, 0.40);
    EXPECT_GT(flowers, 0.01);
    EXPECT_LT(flowers, 0.10);
    for (block_type f : {DANDELION, POPPY, OXEYE_DAISY, CORNFLOWER}) EXPECT_GT(n[f], 0) << f;

    EXPECT_EQ(plains::groundCover(biomegen::GroundSite{SAND, 0, 5, 5, SEED}), AIR);
    EXPECT_EQ(plains::groundCover(biomegen::GroundSite{DIRT, 0, 5, 5, SEED}), AIR);
}

TEST(PlainsTest, FlowersGrowInSingleKindPatches) {
    // Most flowers in a patch are its own kind, and patches differ.
    std::set<block_type> kinds;
    int inPatch = 0, matching = 0;
    for (int z = 0; z < 300; z++)
        for (int x = 0; x < 300; x++) {
            double s = 0.0;
            const block_type patch = plains::meadowAt(x, z, SEED, s);
            if (patch == AIR) continue;
            kinds.insert(patch);
            const block_type b = plains::groundCover(grassAt(x, z));
            if (b != DANDELION && b != POPPY && b != OXEYE_DAISY && b != CORNFLOWER) continue;
            inPatch++;
            if (b == patch) matching++;
        }
    EXPECT_EQ(kinds.size(), 4u);
    ASSERT_GT(inPatch, 100);
    EXPECT_GT(static_cast<double>(matching) / inPatch, 0.7);
}

TEST(PlainsTest, PlainsChunksAreOpenWithFlowersAndFewTrees) {
    // (208, -1776) is the middle of a flat plains region for seed 42.
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    long plainsCols = 0, tallGrass = 0, flowers = 0, newFlowers = 0;
    int trunks = 0;
    for (int cx = 10; cx < 16; cx++)
        for (int cz = -114; cz < -108; cz++) {
            GenChunk c(tg, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    if (h + 1 >= CHUNK_HEIGHT) continue;
                    const block_type above = c.at(x, h + 1, z);
                    if (c.biomes[x][z] != BIOME_PLAINS) {
                        EXPECT_NE(above, OXEYE_DAISY) << "plains-only flower";
                        EXPECT_NE(above, CORNFLOWER) << "plains-only flower";
                        continue;
                    }
                    plainsCols++;
                    if (above == TALL_GRASS) tallGrass++;
                    if (above == DANDELION || above == POPPY || above == OXEYE_DAISY || above == CORNFLOWER) flowers++;
                    if (above == OXEYE_DAISY || above == CORNFLOWER) newFlowers++;
                    if (c.at(x, h, z) == GRASS && above == WOOD) trunks++;
                }
        }
    ASSERT_GT(plainsCols, 5000);
    EXPECT_GT(static_cast<double>(tallGrass) / plainsCols, 0.08);
    EXPECT_GT(flowers, 50);
    EXPECT_GT(newFlowers, 5);
    // 36 chunks ≈ 9200 columns: open land, a handful of trees at most.
    EXPECT_LT(trunks, 40);
}

TEST(PlainsTest, TreeRateReadsAsOpenLand) {
    // Expected trees per tree cell: the copses' share of the land (70% of
    // their cells grow) plus the lone-oak rate. A forest offers ~0.6.
    const double copseShare = static_cast<double>(plains::COPSE_CHANCE) / 100.0 * 3.14159 * plains::COPSE_RADIUS *
                              plains::COPSE_RADIUS / (plains::COPSE_CELL * plains::COPSE_CELL);
    const double perCell = copseShare * 0.7 + static_cast<double>(plains::LONE_TREE_PER_MILLE) / 1000.0;
    EXPECT_LT(perCell, 0.03);
    EXPECT_GT(perCell, 0.005) << "but not treeless";
}
