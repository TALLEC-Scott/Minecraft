// Taiga (src/biomes/taiga.cpp): spruce in three shapes, podzol floor
// patches, mossy boulders, ferns. Generated through fillChunkTerrain.
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

class TaigaTest : public ::testing::Test {
  protected:
    TerrainGenerator terrain{42, 0.1f, 0, CHUNK_HEIGHT};

    // Chunk coordinates of the corner of a W×W chunk block that is mostly taiga.
    bool findTaiga(int w, int& outX, int& outZ) {
        for (int r = 0; r < 120; r += 2)
            for (int cx = -r; cx <= r; cx += 2)
                for (int cz = -r; cz <= r; cz += 2) {
                    if (std::abs(cx) != r && std::abs(cz) != r) continue;
                    int taiga = 0, total = 0;
                    for (int x = 0; x < w * CHUNK_SIZE; x += 4)
                        for (int z = 0; z < w * CHUNK_SIZE; z += 4) {
                            Biome b;
                            terrain.getHeightAndBiome(cx * CHUNK_SIZE + x, cz * CHUNK_SIZE + z, b);
                            taiga += b == BIOME_TAIGA;
                            total++;
                        }
                    if (taiga * 10 >= total * 9) {
                        outX = cx;
                        outZ = cz;
                        return true;
                    }
                }
        return false;
    }
};

} // namespace

TEST_F(TaigaTest, HasSpruceVarietyPodzolFernsAndBoulders) {
    int cx = 0, cz = 0;
    ASSERT_TRUE(findTaiga(6, cx, cz)) << "no taiga region found for seed 42";
    Region r(terrain, cx, cz, 6);
    int trunks = 0, giant = 0, tall = 0, podzol = 0, grass = 0, ferns = 0, mossy = 0, otherLogs = 0;
    for (int x = 0; x < r.size(); x++)
        for (int z = 0; z < r.size(); z++) {
            if (r.biome(x, z) != BIOME_TAIGA) continue;
            const int h = r.height(x, z);
            const block_type ground = r.at(x, h, z), above = r.at(x, h + 1, z);
            podzol += ground == PODZOL;
            grass += ground == GRASS;
            ferns += above == FERN;
            for (int y = h - 1; y <= h + 3; y++) mossy += r.at(x, y, z) == MOSSY_COBBLESTONE;
            if (isLog(above) && above != SPRUCE_LOG) otherLogs++;
            if (above != SPRUCE_LOG) continue;
            int top = h + 1;
            while (r.at(x, top + 1, z) == SPRUCE_LOG) top++;
            const bool wide = x + 1 < r.size() && z + 1 < r.size() && r.at(x + 1, h + 1, z) == SPRUCE_LOG &&
                              r.at(x, h + 1, z + 1) == SPRUCE_LOG && r.at(x + 1, h + 1, z + 1) == SPRUCE_LOG;
            // A giant's 2x2 trunk is sunk into the ground (its log replaces the surface block).
            if (wide && ground == SPRUCE_LOG && top - h >= 14) giant++;
            if (ground == SPRUCE_LOG) continue; // count each 1x1 trunk once, at its base
            trunks++;
            // Tall thin: a bare lower trunk (the stock spruce has leaves down
            // to 3 above the ground).
            if (!wide && top - h >= 9 && x > 0 && x + 1 < r.size() && r.at(x + 1, h + 3, z) == AIR &&
                     r.at(x - 1, h + 3, z) == AIR)
                tall++;
        }
    EXPECT_EQ(otherLogs, 0) << "taiga grows only spruce";
    EXPECT_GT(trunks, 30);
    EXPECT_GT(giant, 0) << "no giant 2x2 spruce";
    EXPECT_GT(tall, 0) << "no tall thin spruce";
    EXPECT_GT(podzol, 50) << "no podzol patches";
    EXPECT_GT(grass, podzol / 2) << "podzol should be patches, not the whole floor";
    EXPECT_GT(ferns, 20);
    EXPECT_GT(mossy, 0) << "no mossy boulders";
}

TEST_F(TaigaTest, CanopiesStayWithinTreeMaxRadiusAndMatchAcrossBorders) {
    int cx = 0, cz = 0;
    ASSERT_TRUE(findTaiga(4, cx, cz));
    Region r(terrain, cx, cz, 4);
    // Every spruce leaf in the interior has a spruce log within
    // TREE_MAX_RADIUS columns (the canopy reach rule).
    const int R = biomegen::TREE_MAX_RADIUS, n = r.size();
    for (int x = R; x < n - R; x++)
        for (int z = R; z < n - R; z++)
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (r.at(x, y, z) != SPRUCE_LEAVES) continue;
                bool near = false;
                for (int dx = -R; dx <= R && !near; dx++)
                    for (int dz = -R; dz <= R && !near; dz++)
                        for (int dy = -1; dy <= 22 && !near; dy++) near = r.at(x + dx, y - dy, z + dz) == SPRUCE_LOG;
                ASSERT_TRUE(near) << "stray leaf at region (" << x << ", " << y << ", " << z << ")";
            }
    // Chunks generated alone agree with the region (deterministic, border-safe).
    GenChunk lone(terrain, cx + 1, cz + 1);
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++)
            for (int y = 0; y < CHUNK_HEIGHT; y++)
                ASSERT_EQ(lone.at(x, y, z), r.at(CHUNK_SIZE + x, y, CHUNK_SIZE + z));
}

TEST_F(TaigaTest, PodzolAndMossyCobblestoneStayInTaiga) {
    // Podzol may spill a few columns past a taiga cell; never far.
    long podzol = 0, farPodzol = 0, mossy = 0, farMossy = 0;
    for (int cx = -30; cx < 30; cx += 6)
        for (int cz = -30; cz < 30; cz += 6) {
            GenChunk c(terrain, cx, cz);
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    bool nearTaiga = false;
                    for (int dx = -8; dx <= 8 && !nearTaiga; dx += 4)
                        for (int dz = -8; dz <= 8 && !nearTaiga; dz += 4) {
                            Biome b;
                            terrain.getHeightAndBiome(cx * CHUNK_SIZE + x + dx, cz * CHUNK_SIZE + z + dz, b);
                            nearTaiga = b == BIOME_TAIGA;
                        }
                    const int h = c.heights[x][z];
                    for (int y = h - 1; y <= h + 3; y++) {
                        const block_type t = c.at(x, y, z);
                        if (t == PODZOL) {
                            podzol++;
                            farPodzol += !nearTaiga;
                        }
                        if (t == MOSSY_COBBLESTONE) {
                            mossy++;
                            farMossy += !nearTaiga;
                        }
                    }
                }
        }
    EXPECT_GT(podzol, 0) << "sample holds no taiga";
    EXPECT_EQ(farPodzol, 0);
    EXPECT_EQ(farMossy, 0);
    (void)mossy;
}
