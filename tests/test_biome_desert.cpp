// Desert (src/biomes/desert.cpp): sand over a sandstone layer, cacti that
// never touch, dead bushes, and nothing of it spilling into neighbours.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "TerrainGenerator.h"
#include "block_layers.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr int SEA = CHUNK_HEIGHT / 2;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int cx, int cz) { fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes); }
    block_type at(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// Chunks with at least some desert in them (seed 42 has a desert around
// x -1400, z -300), interior and border alike.
class DesertTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        terrain = new TerrainGenerator(42, 0.1f, 0, CHUNK_HEIGHT);
        chunks = new std::vector<std::unique_ptr<GenChunk>>();
        for (int cx = -95; cx < -78 && chunks->size() < 48; cx++)
            for (int cz = -30; cz < 0 && chunks->size() < 48; cz++) {
                int desert = 0;
                for (int x = 0; x < CHUNK_SIZE; x += 5)
                    for (int z = 0; z < CHUNK_SIZE; z += 5)
                        if (terrain->getBiome(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z) == BIOME_DESERT) desert++;
                if (desert > 0) chunks->push_back(std::make_unique<GenChunk>(*terrain, cx, cz));
            }
    }
    static void TearDownTestSuite() {
        delete chunks;
        delete terrain;
    }
    static TerrainGenerator* terrain;
    static std::vector<std::unique_ptr<GenChunk>>* chunks;
};
TerrainGenerator* DesertTest::terrain = nullptr;
std::vector<std::unique_ptr<GenChunk>>* DesertTest::chunks = nullptr;

} // namespace

TEST_F(DesertTest, FoundDesertChunks) {
    ASSERT_GE(chunks->size(), 20u);
}

TEST_F(DesertTest, SandOverSandstone) {
    int sandCols = 0, withSandstone = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (c->biomes[x][z] != BIOME_DESERT) continue;
                const int h = c->heights[x][z];
                if (h <= SEA + 1 || c->at(x, h, z) != SAND) continue;
                bool cave = false;
                for (int y = h - 3; y < h; y++) cave = cave || c->at(x, y, z) == AIR || c->at(x, y, z) == LAVA;
                if (cave) continue;
                sandCols++;
                // Three blocks of loose sand on top...
                EXPECT_EQ(c->at(x, h - 1, z), SAND);
                EXPECT_EQ(c->at(x, h - 2, z), SAND);
                // ...then sandstone (unless a cave cuts through).
                if (c->at(x, h - 3, z) == SANDSTONE) withSandstone++;
            }
    ASSERT_GT(sandCols, 1000);
    EXPECT_GT(withSandstone, sandCols * 8 / 10) << withSandstone << " of " << sandCols;
}

TEST_F(DesertTest, NothingDesertOutsideTheDesert) {
    // Sandstone, cacti and dead bushes only in desert columns (the old
    // generator scattered cacti on any sand, beaches included).
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (c->biomes[x][z] == BIOME_DESERT) continue;
                for (int y = 1; y < CHUNK_HEIGHT; y++) {
                    const block_type t = c->at(x, y, z);
                    ASSERT_TRUE(t != SANDSTONE && t != CACTUS && t != DEAD_BUSH)
                        << "block " << t << " in biome " << c->biomes[x][z];
                }
            }
}

TEST_F(DesertTest, CactiAndDeadBushes) {
    int cacti = 0, bushes = 0, tall = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                const int h = c->heights[x][z];
                const block_type above = c->at(x, h + 1, z);
                if (above == DEAD_BUSH) {
                    bushes++;
                    EXPECT_EQ(c->at(x, h, z), SAND);
                }
                for (int y = 1; y < CHUNK_HEIGHT; y++) {
                    if (c->at(x, y, z) != CACTUS || c->at(x, y - 1, z) == CACTUS) continue;
                    cacti++;
                    EXPECT_EQ(c->at(x, y - 1, z), SAND) << "cactus grows on sand";
                    int n = 0;
                    while (c->at(x, y + n, z) == CACTUS) {
                        // Cacti never touch each other sideways.
                        for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                            EXPECT_NE(c->at(x + dx, y + n, z + dz), CACTUS);
                        n++;
                    }
                    EXPECT_LE(n, 3);
                    if (n == 3) tall++;
                }
            }
    EXPECT_GT(cacti, 30);
    EXPECT_GT(tall, 5);
    EXPECT_GT(bushes, 30);
}

TEST(DesertBlocksTest, SandstoneAndDeadBush) {
    EXPECT_TRUE(hasFlag(SANDSTONE, BF_SOLID) && hasFlag(SANDSTONE, BF_OPAQUE));
    EXPECT_TRUE(hasFlag(DEAD_BUSH, BF_CROSS));
    EXPECT_FALSE(hasFlag(DEAD_BUSH, BF_SOLID));
    // Sandstone needs a pickaxe; a dead bush breaks instantly into 0..2 sticks.
    EXPECT_EQ(blockDrop(SANDSTONE, ITEM_EMPTY, 0.5f).count, 0);
    EXPECT_EQ(blockDrop(SANDSTONE, ITEM_WOODEN_PICKAXE, 0.5f).id, blockItem(SANDSTONE));
    EXPECT_EQ(breakTicks(DEAD_BUSH, ITEM_EMPTY), 0);
    EXPECT_EQ(blockDrop(DEAD_BUSH, ITEM_EMPTY, 0.1f).count, 0);
    EXPECT_EQ(blockDrop(DEAD_BUSH, ITEM_EMPTY, 0.5f).id, ITEM_STICK);
    EXPECT_EQ(blockDrop(DEAD_BUSH, ITEM_EMPTY, 0.9f).count, 2);
}

TEST(DesertBlocks, CactusHasItsOwnTopAndBottom) {
    using block_layers::layerForFace;
    // Faces: 0..3 = sides, 4/5 = ±Y.
    for (int face = 0; face < 4; ++face) EXPECT_EQ(layerForFace(CACTUS, face), block_layers::CACTUS_LAYER);
    EXPECT_EQ(layerForFace(CACTUS, 4), block_layers::CACTUS_TOP_LAYER);
    EXPECT_EQ(layerForFace(CACTUS, 5), block_layers::CACTUS_TOP_LAYER);
}
