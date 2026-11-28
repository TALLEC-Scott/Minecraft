// Tundra (src/biomes/tundra.cpp): snow cover, frozen still water, sparse
// spruce, rare boulders / ice spikes. GL-free, through fillChunkTerrain.

#include <gtest/gtest.h>

#include <memory>

#include "TerrainGenerator.h"
#include "biome.h"
#include "mining.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

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

class TundraTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};
    int sea = terrain.getSeaLevel();
};

// Chunks around a frozen tundra river and a tundra interior on seed 42
// (found with a scan of getHeightAndBiome).
constexpr int RIVER_CX = -464 / CHUNK_SIZE, RIVER_CZ = 1072 / CHUNK_SIZE;
constexpr int INNER_CX = -2144 / CHUNK_SIZE, INNER_CZ = 768 / CHUNK_SIZE;

} // namespace

TEST(TundraDef, SnowSurfaceGravelShoreAndIce) {
    const BiomeDef& d = biomeDef(BIOME_TUNDRA);
    EXPECT_EQ(d.surface, SNOW);
    EXPECT_EQ(d.shore, GRAVEL);
    EXPECT_NE(d.decorate, nullptr);
    EXPECT_TRUE(hasFlag(ICE, BF_SOLID | BF_OPAQUE));
    EXPECT_EQ(blockDrop(ICE, ITEM_WOODEN_PICKAXE, 0.5f).count, 0); // ice drops nothing
    EXPECT_GT(breakTicks(ICE, ITEM_WOODEN_PICKAXE), 0);
}

TEST_F(TundraTest, StillWaterFreezesOnlyInTundra) {
    int tundraIce = 0, tundraOpen = 0, otherIce = 0, scanned = 0;
    for (int cx = RIVER_CX - 6; cx <= RIVER_CX + 6; cx++)
        for (int cz = RIVER_CZ - 6; cz <= RIVER_CZ + 6; cz++) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    const block_type top = c.at(x, sea, z);
                    if (top != ICE && top != WATER) continue;
                    scanned++;
                    const bool tundra = c.biomes[x][z] == BIOME_TUNDRA;
                    if (top == ICE) {
                        EXPECT_NE(c.at(x, sea - 1, z), AIR); // a lid on water (or on the river bed)
                        if (!tundra) otherIce++;
                        if (tundra) tundraIce++;
                    } else if (tundra && c.at(x, sea + 1, z) == AIR) {
                        tundraOpen++;
                    }
                }
        }
    EXPECT_GT(scanned, 0);
    EXPECT_GT(tundraIce, 50);
    EXPECT_EQ(otherIce, 0);
    // Open water inside tundra only where a canopy shaded the cell's site.
    EXPECT_LT(tundraOpen, tundraIce / 10 + 16);
}

TEST_F(TundraTest, SnowFieldWithSparseSpruce) {
    int snow = 0, cols = 0, trunks = 0;
    for (int cx = INNER_CX - 3; cx <= INNER_CX + 3; cx++)
        for (int cz = INNER_CZ - 3; cz <= INNER_CZ + 3; cz++) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (c.biomes[x][z] != BIOME_TUNDRA) continue;
                    const int h = c.heights[x][z];
                    if (h <= sea + 1) continue;
                    cols++;
                    if (c.at(x, h, z) == SNOW) snow++;
                    if (c.at(x, h + 1, z) == SPRUCE_LOG) trunks++;
                    // Nothing but trees, rocks and ice ever stands on tundra snow.
                    const block_type above = c.at(x, h + 1, z);
                    EXPECT_TRUE(above == AIR || above == SPRUCE_LOG || above == SPRUCE_LEAVES || above == ICE ||
                                above == STONE || above == COBBLESTONE || above == SNOW)
                        << static_cast<int>(above);
                }
        }
    ASSERT_GT(cols, 5000);
    EXPECT_GT(snow, cols * 8 / 10);
    // Sparse: well under one tree per 64 columns on average, but some.
    EXPECT_GT(trunks, 0);
    EXPECT_LT(trunks, cols / 64);
}

TEST_F(TundraTest, Deterministic) {
    GenChunk a(terrain, RIVER_CX, RIVER_CZ), b(terrain, RIVER_CX, RIVER_CZ);
    for (size_t i = 0; i < VOLUME; i++) ASSERT_EQ(a.blocks[i].getType(), b.blocks[i].getType());
}
