// Jungle (src/biomes/jungle.cpp): 2×2 jungle-log giants, middle
// storey trees, jungle-leaf crowns and floor bushes, real vines (VINE cells
// with their sides in the state byte) hanging off crowns and trunks, a fern /
// tall-grass floor that still leaves open ground, and no jungle wood outside
// the jungle.
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "TerrainGenerator.h"
#include "biome.h"
#include "mining.h"
#include "terrain_fill.h"
#include "vine.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
constexpr int SEA = CHUNK_HEIGHT / 2;

struct GenChunk {
    int cx, cz;
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    std::unique_ptr<uint8_t[]> states{new uint8_t[VOLUME]()};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int x, int z) : cx(x), cz(z) {
        fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes, nullptr, states.get());
    }
    uint8_t state(int x, int y, int z) const {
        return states[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z];
    }
    block_type at(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// Seed 42 has a jungle around x -450, z -100; take the chunks there with
// any jungle in them, interior and border alike.
class JungleTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        terrain = new TerrainGenerator(42, 0.1f, 0, CHUNK_HEIGHT);
        chunks = new std::vector<std::unique_ptr<GenChunk>>();
        for (int cx = -34; cx < -20 && chunks->size() < 48; cx++)
            for (int cz = -12; cz < 0 && chunks->size() < 48; cz++) {
                int rain = 0;
                for (int x = 0; x < CHUNK_SIZE; x += 5)
                    for (int z = 0; z < CHUNK_SIZE; z += 5)
                        if (terrain->getBiome(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z) == BIOME_JUNGLE) rain++;
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
TerrainGenerator* JungleTest::terrain = nullptr;
std::vector<std::unique_ptr<GenChunk>>* JungleTest::chunks = nullptr;

} // namespace

TEST_F(JungleTest, FoundJungleChunks) {
    ASSERT_GE(chunks->size(), 20u);
}

TEST_F(JungleTest, JungleWoodStaysNearTheJungle) {
    // Every jungle log belongs to a tree rooted in a jungle column, at
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
                            near = terrain->getBiome(wx + dx, wz + dz) == BIOME_JUNGLE;
                    ASSERT_TRUE(near) << "jungle log at " << wx << "," << y << "," << wz;
                }
    EXPECT_GT(logs, 500);
}

TEST_F(JungleTest, GiantTreesHaveTwoByTwoTrunks) {
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

TEST_F(JungleTest, CanopiesStayWithinTreeRadius) {
    // Every leaf or vine in the middle of a chunk has a log within
    // TREE_MAX_RADIUS columns (crowns, branch balls and vines all hang off a nearby trunk),
    // so a canopy never outgrows the margin chunks sample across borders.
    constexpr int R = biomegen::TREE_MAX_RADIUS;
    int checked = 0;
    for (const auto& c : *chunks)
        for (int x = R; x < CHUNK_SIZE - R; x++)
            for (int z = R; z < CHUNK_SIZE - R; z++) {
                if (c->biomes[x][z] != BIOME_JUNGLE) continue;
                for (int y = SEA; y < CHUNK_HEIGHT; y++) {
                    if (c->at(x, y, z) != JUNGLE_LEAVES && c->at(x, y, z) != VINE) continue;
                    checked++;
                    bool log = false;
                    for (int dx = -R; dx <= R && !log; dx++)
                        for (int dz = -R; dz <= R && !log; dz++)
                            for (int dy = -8; dy <= 10 && !log; dy++) log = isLog(c->at(x + dx, y + dy, z + dz));
                    ASSERT_TRUE(log) << "stray leaf at " << x << "," << y << "," << z;
                }
            }
    EXPECT_GT(checked, 1000);
}

TEST_F(JungleTest, VinesHangFromCanopiesAndTrunks) {
    // Every vine has a valid state; each side it covers lies against a block
    // that can hold it, or continues the chain of the vine above; it never
    // rests on the ground. Some hang long, down toward the floor.
    int vines = 0, onLeaves = 0, onLogs = 0, chained = 0, low = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 1; y + 1 < CHUNK_HEIGHT; y++) {
                    if (c->at(x, y, z) != VINE) continue;
                    vines++;
                    const uint8_t s = c->state(x, y, z);
                    ASSERT_TRUE(vine::validState(s)) << "vine without sides at " << x << "," << y << "," << z;
                    const block_type below = c->at(x, y - 1, z);
                    // (A later tree may grow a branch or crown under it.)
                    EXPECT_TRUE(below == AIR || below == VINE || isLog(below) || isLeaves(below))
                        << "vine resting on " << int(below);
                    if (y - 1 - c->heights[x][z] <= 3) low++;
                    for (int b = 0; b < 4; b++) {
                        if ((s & (1u << b)) == 0) continue;
                        const int nx = x + vine::SIDE[b][0], nz = z + vine::SIDE[b][2];
                        if (nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue; // other chunk
                        const block_type wall = c->at(nx, y, nz);
                        if (isLeaves(wall)) {
                            onLeaves++;
                        } else if (isLog(wall)) {
                            onLogs++;
                        } else {
                            ASSERT_EQ(c->at(x, y + 1, z), VINE) << "floating vine at " << x << "," << y << "," << z;
                            EXPECT_NE(c->state(x, y + 1, z) & (1u << b), 0) << "chain keeps its side";
                            chained++;
                        }
                    }
                }
    EXPECT_GT(vines, 2000);
    EXPECT_GT(onLeaves, 300);
    EXPECT_GT(onLogs, 100);
    EXPECT_GT(chained, 500);
    EXPECT_GT(low, 50) << "some vines reach down near the floor";
}

TEST_F(JungleTest, NoHangingLeafStrands) {
    // The old fake vines: a leaf with air on all four sides and below. Only
    // the odd crown tip may look like that, never in numbers.
    int strands = 0, leaves = 0;
    for (const auto& c : *chunks)
        for (int x = 1; x + 1 < CHUNK_SIZE; x++)
            for (int z = 1; z + 1 < CHUNK_SIZE; z++)
                for (int y = SEA; y + 1 < CHUNK_HEIGHT; y++) {
                    if (!isLeaves(c->at(x, y, z))) continue;
                    leaves++;
                    if (isLeaves(c->at(x, y + 1, z)) && c->at(x, y - 1, z) == AIR && c->at(x + 1, y, z) == AIR &&
                        c->at(x - 1, y, z) == AIR && c->at(x, y, z + 1) == AIR && c->at(x, y, z - 1) == AIR)
                        strands++;
                }
    ASSERT_GT(leaves, 5000);
    EXPECT_LT(strands, leaves / 200);
}

TEST_F(JungleTest, JungleLeavesFormTheCanopy) {
    int jungle = 0, oak = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (c->biomes[x][z] != BIOME_JUNGLE) continue;
                for (int y = SEA; y < CHUNK_HEIGHT; y++) {
                    jungle += c->at(x, y, z) == JUNGLE_LEAVES;
                    oak += c->at(x, y, z) == LEAVES;
                }
            }
    EXPECT_GT(jungle, 5000);
    EXPECT_LT(oak, jungle / 50) << "only neighbours' oaks reaching across the border";
}

TEST_F(JungleTest, BushesAreMoundsNotCubes) {
    // A bush: a jungle log on the ground with jungle leaves on top of it.
    // Its mound spreads over at least five of the eight columns around it.
    int bushes = 0, small = 0;
    for (const auto& c : *chunks)
        for (int x = 1; x + 1 < CHUNK_SIZE; x++)
            for (int z = 1; z + 1 < CHUNK_SIZE; z++) {
                const int y = c->heights[x][z] + 1;
                if (c->biomes[x][z] != BIOME_JUNGLE || c->at(x, y, z) != JUNGLE_LOG ||
                    c->at(x, y + 1, z) != JUNGLE_LEAVES)
                    continue;
                bushes++;
                int around = 0;
                for (int dx = -1; dx <= 1; dx++)
                    for (int dz = -1; dz <= 1; dz++)
                        if ((dx || dz) && c->at(x + dx, y, z + dz) == JUNGLE_LEAVES) around++;
                if (around < 5) small++;
            }
    EXPECT_GT(bushes, 30);
    EXPECT_LT(small, bushes / 4) << "some squeezed by slopes or neighbours, not lone cubes";
}

TEST_F(JungleTest, FloorIsMostlyUndergrowth) {
    // Land columns: most are under a bush, a trunk or tall grass; bare
    // grass is the exception and ferns (sapling-like sprites) are rare.
    int land = 0, bush = 0, ferns = 0, grass = 0, bare = 0;
    for (const auto& c : *chunks)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (c->biomes[x][z] != BIOME_JUNGLE) continue;
                const int h = c->heights[x][z];
                if (h <= SEA + 1 || c->at(x, h, z) != GRASS) continue;
                const block_type above = c->at(x, h + 1, z);
                land++;
                if (isLeaves(above) || isLog(above)) bush++;
                if (above == FERN) ferns++;
                if (above == TALL_GRASS) grass++;
                if (above == AIR) bare++;
            }
    ASSERT_GT(land, 1000);
    EXPECT_GT(bush, land * 2 / 5) << "bushes cover much of the floor";
    EXPECT_GT(grass, land / 5);
    EXPECT_GT(ferns, 0);
    EXPECT_LT(ferns, land / 30);
    EXPECT_LT(bare, land / 4) << "open floor is the exception";
}

TEST_F(JungleTest, GrassIsDeepJungleGreen) {
    // Darker than the neighbouring forests' grass, still green.
    const glm::vec3 rain = biomeDef(BIOME_JUNGLE).grassTint;
    EXPECT_LT(rain.g, biomeDef(BIOME_FOREST).grassTint.g);
    EXPECT_LT(rain.g, biomeDef(BIOME_SEASONAL_FOREST).grassTint.g);
    EXPECT_GT(rain.g, rain.r * 1.5f);
    EXPECT_GT(rain.g, rain.b * 1.5f);
}

TEST(JungleBlocksTest, JungleLog) {
    EXPECT_TRUE(isLog(JUNGLE_LOG));
    EXPECT_TRUE(hasFlag(JUNGLE_LOG, BF_SOLID) && hasFlag(JUNGLE_LOG, BF_OPAQUE));
    EXPECT_EQ(blockMining(JUNGLE_LOG).bestTool, ToolKind::Axe);
    EXPECT_EQ(blockDrop(JUNGLE_LOG, ITEM_EMPTY, 0.5f).id, blockItem(JUNGLE_LOG));
    EXPECT_EQ(getStepSound(JUNGLE_LOG), STEP_WOOD);
}
