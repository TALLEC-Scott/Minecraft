// Rainforest (src/biomes/rainforest.cpp): 2×2 jungle-log giants, middle
// storey trees, floor bushes, hanging leaf vines, a fern / tall-grass floor
// that still leaves open ground, and no jungle wood outside the jungle.
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "TerrainGenerator.h"
#include "biome.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr int SEA = CHUNK_HEIGHT / 2;

struct GenChunk {
    int cx, cz;
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int x, int z) : cx(x), cz(z) {
        fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes);
    }
    block_type at(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// Seed 42 has a rainforest around x -450, z -100; take the chunks there with
// any rainforest in them, interior and border alike.
class RainforestTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        terrain = new TerrainGenerator(42, 0.1f, 0, CHUNK_HEIGHT);
        chunks = new std::vector<std::unique_ptr<GenChunk>>();
        for (int cx = -34; cx < -20 && chunks->size() < 48; cx++)
            for (int cz = -12; cz < 0 && chunks->size() < 48; cz++) {
                int rain = 0;
                for (int x = 0; x < CHUNK_SIZE; x += 5)
                    for (int z = 0; z < CHUNK_SIZE; z += 5)
                        if (terrain->getBiome(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z) == BIOME_RAINFOREST) rain++;
                if (rain > 0) chunks->push_back(std::make_unique<GenChunk>(*terrain, cx, cz));
            }
    }
    static void TearDownTestSuite() {
        delete chunks;
        delete terrain;
    }
    static TerrainGenerator* terrain;
    static std::vector<std::unique_ptr<GenChunk>>* chunks;
};
TerrainGenerator* RainforestTest::terrain = nullptr;
std::vector<std::unique_ptr<GenChunk>>* RainforestTest::chunks = nullptr;

} // namespace

TEST_F(RainforestTest, FoundRainforestChunks) {
    ASSERT_GE(chunks->size(), 20u);
}

TEST_F(RainforestTest, JungleWoodStaysNearTheJungle) {
    // Every jungle log belongs to a tree rooted in a rainforest column, at
    // most TREE_MAX_RADIUS away (branches reach out from the trunk).
    int logs = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 1; y < CHUNK_HEIGHT; y++) {
                    if (c->at(x, y, z) != JUNGLE_LOG) continue;
                    logs++;
                    const int wx = c->cx * CHUNK_SIZE + x, wz = c->cz * CHUNK_SIZE + z;
                    bool near = false;
                    for (int dx = -biomegen::TREE_MAX_RADIUS; dx <= biomegen::TREE_MAX_RADIUS && !near; dx++)
                        for (int dz = -biomegen::TREE_MAX_RADIUS; dz <= biomegen::TREE_MAX_RADIUS && !near; dz++)
                            near = terrain->getBiome(wx + dx, wz + dz) == BIOME_RAINFOREST;
                    ASSERT_TRUE(near) << "jungle log at " << wx << "," << y << "," << wz;
                }
    EXPECT_GT(logs, 500);
}

TEST_F(RainforestTest, GiantTreesHaveTwoByTwoTrunks) {
    // A 2×2 block of jungle logs 10 above the ground is a giant's trunk.
    int giants = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x + 1 < CHUNK_SIZE; x++)
            for (int z = 0; z + 1 < CHUNK_SIZE; z++) {
                const int y = c->heights[x][z] + 10;
                if (c->at(x, y, z) == JUNGLE_LOG && c->at(x + 1, y, z) == JUNGLE_LOG &&
                    c->at(x, y, z + 1) == JUNGLE_LOG && c->at(x + 1, y, z + 1) == JUNGLE_LOG)
                    giants++;
            }
    EXPECT_GT(giants, 10);
}

TEST_F(RainforestTest, CanopiesStayWithinTreeRadius) {
    // Every leaf in the middle of a chunk has a log within TREE_MAX_RADIUS
    // columns (crowns, branch balls and vines all hang off a nearby trunk),
    // so a canopy never outgrows the margin chunks sample across borders.
    constexpr int R = biomegen::TREE_MAX_RADIUS;
    int checked = 0;
    for (const auto& c : *chunks)
        for (int x = R; x < CHUNK_SIZE - R; x++)
            for (int z = R; z < CHUNK_SIZE - R; z++) {
                if (c->biomes[x][z] != BIOME_RAINFOREST) continue;
                for (int y = SEA; y < CHUNK_HEIGHT; y++) {
                    if (c->at(x, y, z) != LEAVES) continue;
                    checked++;
                    bool log = false;
                    for (int dx = -R; dx <= R && !log; dx++)
                        for (int dz = -R; dz <= R && !log; dz++)
                            for (int dy = -8; dy <= 10 && !log; dy++)
                                log = isLog(c->at(x + dx, y + dy, z + dz));
                    ASSERT_TRUE(log) << "stray leaf at " << x << "," << y << "," << z;
                }
            }
    EXPECT_GT(checked, 1000);
}

TEST_F(RainforestTest, VinesHangFromCanopies) {
    // A vine: a leaf with air on all four sides and below, under another leaf.
    int vines = 0;
    for (const auto& c : *chunks)
        for (int x = 1; x + 1 < CHUNK_SIZE; x++)
            for (int z = 1; z + 1 < CHUNK_SIZE; z++)
                for (int y = SEA; y + 1 < CHUNK_HEIGHT; y++)
                    if (c->at(x, y, z) == LEAVES && c->at(x, y + 1, z) == LEAVES && c->at(x, y - 1, z) == AIR &&
                        c->at(x + 1, y, z) == AIR && c->at(x - 1, y, z) == AIR && c->at(x, y, z + 1) == AIR &&
                        c->at(x, y, z - 1) == AIR)
                        vines++;
    EXPECT_GT(vines, 50);
}

TEST_F(RainforestTest, LushButWalkableFloor) {
    int open = 0, ferns = 0, grass = 0, bare = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (c->biomes[x][z] != BIOME_RAINFOREST) continue;
                const int h = c->heights[x][z];
                if (h <= SEA + 1 || c->at(x, h, z) != GRASS) continue;
                const block_type above = c->at(x, h + 1, z);
                open++;
                if (above == FERN) ferns++;
                if (above == TALL_GRASS) grass++;
                if (above == AIR) bare++;
            }
    ASSERT_GT(open, 1000);
    EXPECT_GT(ferns, open / 20);
    EXPECT_GT(grass, open / 5);
    EXPECT_GT(bare, open / 5) << "some floor stays open";
}

TEST(RainforestBlocksTest, JungleLog) {
    EXPECT_TRUE(isLog(JUNGLE_LOG));
    EXPECT_TRUE(hasFlag(JUNGLE_LOG, BF_SOLID) && hasFlag(JUNGLE_LOG, BF_OPAQUE));
    EXPECT_EQ(blockMining(JUNGLE_LOG).bestTool, ToolKind::Axe);
    EXPECT_EQ(blockDrop(JUNGLE_LOG, ITEM_EMPTY, 0.5f).id, blockItem(JUNGLE_LOG));
    EXPECT_EQ(getStepSound(JUNGLE_LOG), STEP_WOOD);
}
