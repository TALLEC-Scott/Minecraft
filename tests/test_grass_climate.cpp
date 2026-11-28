// Climate-driven grass (grassDensity / tallGrassShare / climateGrass in
// biome.h), two-block plants (double_plant.h) and climate-shifted grass /
// foliage colours (climateTint, biome_tint.h).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "TerrainGenerator.h"
#include "biome.h"
#include "biome_tint.h"
#include "block_layers.h"
#include "double_plant.h"
#include "item.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr uint64_t SEED = 42;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];
    BiomeTints tints{};

    GenChunk(TerrainGenerator& tg, int cx, int cz) {
        fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes, &tints);
    }
    block_type at(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

} // namespace

TEST(GrassClimateTest, DensityIsBoundedAndSmooth) {
    constexpr int N = 200;
    constexpr double STEP = 1.0 / N;
    for (int a = 0; a <= N; a++)
        for (int b = 0; b <= N; b++) {
            const double t = a * STEP, h = b * STEP;
            const float d = grassDensity(t, h);
            ASSERT_GE(d, 0.0f);
            ASSERT_LE(d, 0.7f);
            // No thresholds: a 0.005 climate step changes it by little.
            ASSERT_LT(std::abs(d - grassDensity(t + STEP, h)), 0.025f) << t << "," << h;
            ASSERT_LT(std::abs(d - grassDensity(t, h + STEP)), 0.025f) << t << "," << h;
            const float s = tallGrassShare(t, h);
            ASSERT_GE(s, 0.0f);
            ASSERT_LE(s, 0.4f);
            ASSERT_LT(std::abs(s - tallGrassShare(t + STEP, h)), 0.02f);
            ASSERT_LT(std::abs(s - tallGrassShare(t, h + STEP)), 0.02f);
        }
}

TEST(GrassClimateTest, DensityFollowsTheClimate) {
    // Rises with humidity at a temperate temperature.
    for (double h = 0.0; h < 1.0; h += 0.05) EXPECT_GE(grassDensity(0.5, h + 0.05), grassDensity(0.5, h));
    // Falls in the cold, to nothing at the snow line.
    for (double t = 0.1; t < 0.5; t += 0.05) EXPECT_GE(grassDensity(t + 0.05, 0.5), grassDensity(t, 0.5));
    EXPECT_LT(grassDensity(0.15, 0.6), 0.005f) << "tundra";
    EXPECT_LT(grassDensity(0.80, 0.30), 0.03f) << "desert";
    // The Minecraft-ish ladder: lush > temperate > dry.
    const float lush = grassDensity(0.65, 0.75), plains = grassDensity(0.55, 0.45), dry = grassDensity(0.5, 0.25);
    EXPECT_GT(lush, 0.55f);
    EXPECT_GT(plains, 0.3f);
    EXPECT_LT(plains, lush);
    EXPECT_LT(dry, 0.2f);
    // Tall grass: warm grassland over cold or wet forest.
    EXPECT_GT(tallGrassShare(0.66, 0.40), 0.25f) << "savanna";
    EXPECT_GT(tallGrassShare(0.66, 0.40), 2.0f * tallGrassShare(0.30, 0.50)) << "taiga";
    EXPECT_GT(tallGrassShare(0.56, 0.45), 2.0f * tallGrassShare(0.68, 0.80)) << "rainforest";
}

TEST(GrassClimateTest, ClimateGrassMatchesTheDensity) {
    int grass = 0, tall = 0;
    const int side = 200;
    for (int z = 0; z < side; z++)
        for (int x = 0; x < side; x++) {
            const block_type b = biomegen::climateGrass(biomegen::GroundSite{GRASS, 0, x, z, SEED, 0.66, 0.40});
            ASSERT_TRUE(b == AIR || b == TALL_GRASS || b == DOUBLE_TALL_GRASS);
            grass += b != AIR;
            tall += b == DOUBLE_TALL_GRASS;
        }
    const double share = static_cast<double>(grass) / (side * side);
    EXPECT_NEAR(share, grassDensity(0.66, 0.40), 0.02);
    EXPECT_NEAR(static_cast<double>(tall) / grass, tallGrassShare(0.66, 0.40), 0.06);
    EXPECT_EQ(biomegen::climateGrass(biomegen::GroundSite{GRASS, 0, 1, 1, SEED, 0.10, 0.5}), AIR) << "snow line";
}

TEST(GrassClimateTest, TwoBlockPlantHelpers) {
    using namespace double_plant;
    for (block_type lower : {DOUBLE_TALL_GRASS, LARGE_FERN}) {
        EXPECT_TRUE(isLower(lower));
        EXPECT_TRUE(isUpper(upperOf(lower)));
        EXPECT_EQ(lowerOf(upperOf(lower)), lower);
        int dy = 0;
        block_type other = AIR;
        ASSERT_TRUE(partner(lower, dy, other));
        EXPECT_EQ(dy, 1);
        EXPECT_EQ(other, upperOf(lower));
        ASSERT_TRUE(partner(upperOf(lower), dy, other));
        EXPECT_EQ(dy, -1);
        EXPECT_EQ(other, lower);
        for (block_type half : {lower, upperOf(lower)}) {
            EXPECT_TRUE(hasFlag(half, BF_CROSS));
            EXPECT_FALSE(hasFlag(half, BF_SOLID));
            EXPECT_EQ(breakTicks(half, ITEM_EMPTY), 0);
            EXPECT_EQ(blockDrop(half, ITEM_EMPTY, 0.5f).count, 0);
        }
    }
    EXPECT_EQ(shortOf(DOUBLE_TALL_GRASS), TALL_GRASS);
    EXPECT_EQ(shortOf(LARGE_FERN), FERN);
    int dy = 0;
    block_type other = AIR;
    EXPECT_FALSE(partner(TALL_GRASS, dy, other));
    // Each half has its own sprite (76..79, the range the shaders test);
    // the item shows the top half.
    using namespace block_layers;
    EXPECT_EQ(layerForFace(DOUBLE_TALL_GRASS, 0), DOUBLE_TALL_GRASS_BOTTOM_LAYER);
    EXPECT_EQ(layerForFace(DOUBLE_TALL_GRASS_TOP, 0), DOUBLE_TALL_GRASS_TOP_LAYER);
    EXPECT_EQ(layerForFace(LARGE_FERN, 0), LARGE_FERN_BOTTOM_LAYER);
    EXPECT_EQ(layerForFace(LARGE_FERN_TOP, 0), LARGE_FERN_TOP_LAYER);
    EXPECT_EQ(itemIconLayer(blockItem(LARGE_FERN)), LARGE_FERN_TOP_LAYER);
}

// Generated two-block plants always come whole: a lower half with its upper
// half right above, and no stray upper halves. Both kinds appear.
TEST(GrassClimateTest, GeneratedTwoBlockPlantsHaveBothHalves) {
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    int grassPlants = 0, ferns = 0;
    // Savanna / plains (x ≈ -1540, z ≈ -1890) and taiga (x ≈ -176, z ≈ 1520).
    const int areas[2][2] = {{-1540 / CHUNK_SIZE, -1890 / CHUNK_SIZE}, {-176 / CHUNK_SIZE, 1520 / CHUNK_SIZE}};
    for (const auto& a : areas)
        for (int cz = a[1] - 2; cz <= a[1] + 2; cz++)
            for (int cx = a[0] - 2; cx <= a[0] + 2; cx++) {
                GenChunk c(tg, cx, cz);
                for (int x = 0; x < CHUNK_SIZE; x++)
                    for (int z = 0; z < CHUNK_SIZE; z++)
                        for (int y = 1; y < CHUNK_HEIGHT; y++) {
                            const block_type t = c.at(x, y, z);
                            if (double_plant::isLower(t)) {
                                ASSERT_EQ(c.at(x, y + 1, z), double_plant::upperOf(t)) << "upper half on top";
                                grassPlants += t == DOUBLE_TALL_GRASS;
                                ferns += t == LARGE_FERN;
                            } else if (double_plant::isUpper(t)) {
                                ASSERT_EQ(c.at(x, y - 1, z), double_plant::lowerOf(t)) << "stray upper half";
                            }
                        }
            }
    EXPECT_GT(grassPlants, 50);
    EXPECT_GT(ferns, 10);
}

// The amount of grass blends across a biome border: strips of columns
// walking from plains into shrubland change gradually, not in one step.
TEST(GrassClimateTest, GrassAmountFadesAcrossABorder) {
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    // On z = -1808 (scan of seed 42): plains from x = 176, shrubland
    // around 164..180 and 220..240, plains again, taiga from ~290 (where the
    // cold thins the grass out).
    constexpr int Z = -1808, X0 = 176, X1 = 336, STRIP = 16;
    // Fraction of open grass columns with grass in 16-column-wide strips
    // (64 rows deep), along x.
    std::vector<double> frac;
    for (int x0 = X0; x0 < X1; x0 += STRIP) {
        int open = 0, grass = 0;
        for (int czi = (Z - 32) / CHUNK_SIZE - 1; czi <= (Z + 32) / CHUNK_SIZE; czi++) {
            GenChunk c(tg, x0 / CHUNK_SIZE, czi);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const int h = c.heights[x][z];
                    if (c.at(x, h, z) != GRASS && c.at(x, h, z) != PODZOL) continue;
                    const block_type above = c.at(x, h + 1, z);
                    const bool plant = above == TALL_GRASS || above == DOUBLE_TALL_GRASS || above == FERN ||
                                       above == LARGE_FERN;
                    if (above != AIR && !plant) continue; // trees, flowers, logs...
                    open++;
                    grass += plant;
                }
        }
        ASSERT_GT(open, 200);
        frac.push_back(static_cast<double>(grass) / open);
    }
    for (size_t i = 1; i < frac.size(); i++)
        EXPECT_LT(std::abs(frac[i] - frac[i - 1]), 0.12) << "strip " << i << ": " << frac[i - 1] << " -> " << frac[i];
    // ... while it does differ end to end (it isn't flat everywhere).
    EXPECT_GT(std::abs(frac.front() - frac.back()), 0.04);

    // The climate base itself moves only a little per block.
    for (int x = X0; x < X1; x++) {
        Biome b1, b2;
        TerrainGenerator::ColumnClimate c1{}, c2{};
        tg.getHeightBiomeClimate(x, Z, b1, &c1);
        tg.getHeightBiomeClimate(x + 1, Z, b2, &c2);
        EXPECT_LT(std::abs(grassDensity(c1.temperature, c1.humidity) - grassDensity(c2.temperature, c2.humidity)),
                  0.02f);
    }
}

TEST(GrassClimateTest, ClimateTintIsTheBiomeTintAtItsTypicalClimateAndBounded) {
    for (int bi = 0; bi < BIOME_COUNT; bi++) {
        const auto b = static_cast<Biome>(bi);
        const BiomeDef& d = biomeDef(b);
        // Somewhere in the climate plane the shift is the identity.
        float best = 1e9f;
        for (double t = 0.0; t <= 1.0; t += 0.01)
            for (double h = 0.0; h <= 1.0; h += 0.01) {
                const glm::vec3 g = climateTint(b, t, h, false), f = climateTint(b, t, h, true);
                for (int c = 0; c < 3; c++) {
                    ASSERT_GT(g[c], d.grassTint[c] * 0.54f);
                    ASSERT_LT(g[c], d.grassTint[c] * 1.85f);
                    ASSERT_GT(f[c], d.foliageTint[c] * 0.54f);
                    ASSERT_LT(f[c], d.foliageTint[c] * 1.85f);
                    // Continuous in the climate.
                    ASSERT_LT(std::abs(g[c] - climateTint(b, t + 0.01, h, false)[c]), 0.04f);
                    ASSERT_LT(std::abs(g[c] - climateTint(b, t, h + 0.01, false)[c]), 0.04f);
                }
                best = std::min(best, glm::length(g - d.grassTint));
            }
        EXPECT_LT(best, 0.01f) << d.name;
    }
    // Drier is yellower (more red, less blue), wetter bluer-green, colder bluer.
    const glm::vec3 mid = climateTint(BIOME_FOREST, 0.47, 0.58, false);
    const glm::vec3 dry = climateTint(BIOME_FOREST, 0.47, 0.45, false);
    const glm::vec3 cold = climateTint(BIOME_FOREST, 0.37, 0.58, false);
    EXPECT_GT(dry.r, mid.r);
    EXPECT_LT(dry.b, mid.b);
    EXPECT_GT(cold.b, mid.b);
    EXPECT_LT(cold.r, mid.r);
}

// Generated colours change gradually across chunk and biome borders
// (adjacent columns never jump), and a chunk loaded from a save gets exactly
// the colours generation gave it (chunkColumns, WorldSave's path).
TEST(GrassClimateTest, TintsAreContinuousAndMatchTheSavePath) {
    TerrainGenerator tg(SEED, 0.1f, 0, CHUNK_HEIGHT);
    // A row of chunks from plains into shrubland (z = -1808).
    constexpr int CZ = -1808 / CHUNK_SIZE;
    int prev[3] = {-1, -1, -1};
    for (int cx = 176 / CHUNK_SIZE; cx < 336 / CHUNK_SIZE; cx++) {
        GenChunk c(tg, cx, CZ);
        int heights[CHUNK_SIZE][CHUNK_SIZE];
        Biome biomes[CHUNK_SIZE][CHUNK_SIZE];
        BiomeTints loaded{};
        chunkColumns(tg, cx, CZ, heights, biomes, loaded);
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                ASSERT_EQ(heights[x][z], c.heights[x][z]);
                ASSERT_EQ(biomes[x][z], c.biomes[x][z]);
                for (int ch = 0; ch < 3; ch++) {
                    ASSERT_EQ(loaded.grass[x][z][ch], c.tints.grass[x][z][ch]);
                    ASSERT_EQ(loaded.foliage[x][z][ch], c.tints.foliage[x][z][ch]);
                }
            }
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int ch = 0; ch < 3; ch++) {
                const int v = c.tints.grass[x][5][ch];
                if (prev[ch] >= 0) {
                    EXPECT_LE(std::abs(v - prev[ch]), 4) << "x " << cx * CHUNK_SIZE + x;
                }
                prev[ch] = v;
            }
        }
    }
}
