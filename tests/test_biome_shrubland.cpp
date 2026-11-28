// Shrubland (src/biomes/shrubland.cpp): clustered bushes (one-log stumps in
// small leaf blobs), the odd short oak, coarse dirt / gravel patches, dead
// bushes, sparse tall grass. Generated through fillChunkTerrain.
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "TerrainGenerator.h"
#include "biome.h"
#include "terrain_fill.h"

namespace {

constexpr size_t VOLUME = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

struct GenChunk {
    std::unique_ptr<Cube[]> blocks{new Cube[VOLUME]};
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    Biome biomes[CHUNK_SIZE][CHUNK_SIZE];

    GenChunk(TerrainGenerator& tg, int cx, int cz) { fillChunkTerrain(tg, cx, cz, blocks.get(), heights, biomes); }
    block_type at(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return AIR;
        return blocks[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]
            .getType();
    }
};

// A W×W block of chunks stitched into world-space lookups.
struct Region {
    int cx0, cz0, w;
    std::vector<std::unique_ptr<GenChunk>> chunks;
    Region(TerrainGenerator& tg, int cx0_, int cz0_, int w_) : cx0(cx0_), cz0(cz0_), w(w_) {
        for (int i = 0; i < w * w; i++) chunks.push_back(std::make_unique<GenChunk>(tg, cx0 + i / w, cz0 + i % w));
    }
    int size() const { return w * CHUNK_SIZE; }
    const GenChunk& chunk(int x, int z) const { return *chunks[(x / CHUNK_SIZE) * w + z / CHUNK_SIZE]; }
    block_type at(int x, int y, int z) const { return chunk(x, z).at(x % CHUNK_SIZE, y, z % CHUNK_SIZE); }
    int height(int x, int z) const { return chunk(x, z).heights[x % CHUNK_SIZE][z % CHUNK_SIZE]; }
    Biome biome(int x, int z) const { return chunk(x, z).biomes[x % CHUNK_SIZE][z % CHUNK_SIZE]; }
};

class ShrublandTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};

    // Chunk coordinates of the corner of a W×W chunk block whose share of
    // shrubland is >= minPct (and <= maxPct, for border regions).
    bool findRegion(int w, int minPct, int maxPct, int& outX, int& outZ) {
        for (int r = 0; r < 160; r += 2)
            for (int cx = -r; cx <= r; cx += 2)
                for (int cz = -r; cz <= r; cz += 2) {
                    if (std::abs(cx) != r && std::abs(cz) != r) continue;
                    int hit = 0, total = 0;
                    for (int x = 0; x < w * CHUNK_SIZE; x += 4)
                        for (int z = 0; z < w * CHUNK_SIZE; z += 4) {
                            Biome b;
                            terrain.getHeightAndBiome(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z, b);
                            hit += b == BIOME_SHRUBLAND;
                            total++;
                        }
                    if (hit * 100 >= total * minPct && hit * 100 <= total * maxPct) {
                        outX = cx;
                        outZ = cz;
                        return true;
                    }
                }
        return false;
    }
};

} // namespace

TEST_F(ShrublandTest, HasBushesBarePatchesDeadBushesAndFewTrees) {
    int cx = 0, cz = 0;
    ASSERT_TRUE(findRegion(6, 90, 100, cx, cz)) << "no shrubland region found for seed 42";
    Region r(terrain, cx, cz, 6);
    int bushes = 0, trees = 0, coarse = 0, gravel = 0, grass = 0, deadBush = 0, tallGrass = 0, land = 0;
    for (int x = 0; x < r.size(); x++)
        for (int z = 0; z < r.size(); z++) {
            if (r.biome(x, z) != BIOME_SHRUBLAND) continue;
            const int h = r.height(x, z);
            if (h <= terrain.getSeaLevel() + 1) continue;
            land++;
            const block_type top = r.at(x, h, z);
            coarse += top == COARSE_DIRT;
            gravel += top == GRAVEL;
            grass += top == GRASS;
            deadBush += r.at(x, h + 1, z) == DEAD_BUSH;
            tallGrass += r.at(x, h + 1, z) == TALL_GRASS;
            if (r.at(x, h + 1, z) != WOOD) continue;
            int trunk = 0;
            while (r.at(x, h + 1 + trunk, z) == WOOD) trunk++;
            if (trunk <= 2)
                bushes++;
            else
                trees++;
            EXPECT_LE(trunk, 5) << "shrubland oaks stay short";
        }
    ASSERT_GT(land, 2000);
    EXPECT_GT(bushes, 15) << "bushes are the shrubland's main plant";
    EXPECT_LT(trees * 4, bushes) << "proper trees should be rare next to bushes";
    EXPECT_GT(coarse, 20) << "coarse dirt shows through the grass";
    EXPECT_GT(gravel, 0);
    EXPECT_GT(grass, coarse * 4) << "still mostly grass";
    EXPECT_GT(deadBush, 5);
    EXPECT_GT(tallGrass, deadBush);
}

TEST_F(ShrublandTest, BushesAreStumpsInLeafBlobs) {
    int cx = 0, cz = 0;
    ASSERT_TRUE(findRegion(6, 90, 100, cx, cz));
    Region r(terrain, cx, cz, 6);
    int checked = 0, wrapped = 0, low = 0;
    for (int x = 2; x < r.size() - 2; x++)
        for (int z = 2; z < r.size() - 2; z++) {
            const int h = r.height(x, z);
            if (r.biome(x, z) != BIOME_SHRUBLAND || r.at(x, h + 1, z) != WOOD) continue;
            int trunk = 0;
            while (r.at(x, h + 1 + trunk, z) == WOOD) trunk++;
            if (trunk > 2) continue;
            checked++;
            // Capped with leaves, leaves on the sides of the stump's top
            // (unless the slope or a neighbouring plant took the cell).
            const int y = h + trunk;
            EXPECT_EQ(r.at(x, y + 1, z), LEAVES);
            int around = 0;
            for (int d = -1; d <= 1; d += 2) {
                around += r.at(x + d, y, z) == LEAVES;
                around += r.at(x, y, z + d) == LEAVES;
            }
            wrapped += around >= 3;
            // Low: nothing of the bush 4 above the stump top.
            low += r.at(x, y + 4, z) == AIR;
        }
    EXPECT_GT(checked, 10);
    EXPECT_GE(wrapped * 10, checked * 8) << "most bushes wrap their stump in leaves";
    EXPECT_GE(low * 10, checked * 9) << "bushes stay low";
}

TEST_F(ShrublandTest, PatchesAndDeadBushesStayInShrubland) {
    // Coarse dirt is only made by the shrubland; at its borders it must not
    // spill onto a neighbour's columns.
    int cx = 0, cz = 0;
    ASSERT_TRUE(findRegion(6, 30, 70, cx, cz)) << "no shrubland border region found";
    Region r(terrain, cx, cz, 6);
    int coarseHere = 0;
    for (int x = 0; x < r.size(); x++)
        for (int z = 0; z < r.size(); z++) {
            const int h = r.height(x, z);
            const bool coarse = r.at(x, h, z) == COARSE_DIRT;
            if (r.biome(x, z) == BIOME_SHRUBLAND) {
                coarseHere += coarse;
            } else {
                EXPECT_FALSE(coarse) << "coarse dirt leaked into biome " << r.biome(x, z) << " at " << x << "," << z;
            }
        }
    EXPECT_GT(coarseHere, 0);
}

TEST_F(ShrublandTest, ChunksAreDeterministic) {
    int cx = 0, cz = 0;
    ASSERT_TRUE(findRegion(2, 90, 100, cx, cz));
    GenChunk a(terrain, cx, cz), b(terrain, cx, cz);
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < CHUNK_HEIGHT; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) ASSERT_EQ(a.at(x, y, z), b.at(x, y, z));
}
