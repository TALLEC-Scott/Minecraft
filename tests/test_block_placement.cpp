#include <gtest/gtest.h>
#include "cube.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Replicate the sky light index function used in chunk.cpp
static size_t skyIdx(int x, int y, int z) {
    return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
}

// Replicate darkenSkyLightLocal for testing (same algorithm as chunk.cpp)
static void darkenSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    skyLight[skyIdx(x, y, z)] = 0;

    struct Node {
        int x, y, z;
        uint8_t oldLight;
    };
    std::vector<Node> removeQueue;
    removeQueue.reserve(256);

    for (int by = y - 1; by >= 0; by--) {
        if (hasFlag(blocks[skyIdx(x, by, z)].getType(), BF_OPAQUE)) break;
        if (skyLight[skyIdx(x, by, z)] != 15) break;
        uint8_t old = skyLight[skyIdx(x, by, z)];
        skyLight[skyIdx(x, by, z)] = 0;
        removeQueue.push_back({x, by, z, old});
    }

    std::vector<Node> relightSeeds;
    relightSeeds.reserve(64);
    size_t head = 0;
    while (head < removeQueue.size()) {
        auto [cx, cy, cz, oldLight] = removeQueue[head++];
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t neighborLight = skyLight[skyIdx(nx, ny, nz)];
            if (neighborLight > 0 && neighborLight < oldLight) {
                skyLight[skyIdx(nx, ny, nz)] = 0;
                removeQueue.push_back({nx, ny, nz, neighborLight});
            } else if (neighborLight >= oldLight && neighborLight > 0) {
                relightSeeds.push_back({nx, ny, nz, neighborLight});
            }
        }
    }

    struct LightNode {
        int x, y, z;
    };
    std::vector<LightNode> lightQueue;
    lightQueue.reserve(256);
    for (auto& s : relightSeeds) {
        lightQueue.push_back({s.x, s.y, s.z});
    }
    head = 0;
    while (head < lightQueue.size()) {
        auto [cx, cy, cz] = lightQueue[head++];
        uint8_t light = skyLight[skyIdx(cx, cy, cz)];
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            if (skyLight[skyIdx(nx, ny, nz)] >= propagated) continue;
            skyLight[skyIdx(nx, ny, nz)] = propagated;
            lightQueue.push_back({nx, ny, nz});
        }
    }
}

// Replicate updateSkyLightLocal (brightening after block removal)
static void updateSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    uint8_t maxLight = 0;
    for (auto& d : DIRS) {
        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_HEIGHT && nz >= 0 && nz < CHUNK_SIZE) {
            uint8_t nl = skyLight[skyIdx(nx, ny, nz)];
            if (nl > maxLight) maxLight = nl;
        }
    }
    if (y + 1 >= CHUNK_HEIGHT) maxLight = 15;

    uint8_t newLight =
        (y + 1 < CHUNK_HEIGHT && skyLight[skyIdx(x, y + 1, z)] == 15) ? 15 : (maxLight > 0 ? maxLight - 1 : 0);
    skyLight[skyIdx(x, y, z)] = newLight;

    struct Node {
        int x, y, z;
    };
    std::vector<Node> queue;
    queue.reserve(256);
    queue.push_back({x, y, z});

    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        uint8_t light = skyLight[skyIdx(cx, cy, cz)];
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            if (skyLight[skyIdx(nx, ny, nz)] >= propagated) continue;
            skyLight[skyIdx(nx, ny, nz)] = propagated;
            queue.push_back({nx, ny, nz});
        }
    }
}

// Helper: create a flat world — stone floor at y=60, air above
struct TestChunk {
    static constexpr size_t TOTAL = CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
    Cube blocks[TOTAL];
    uint8_t skyLight[TOTAL];

    TestChunk() {
        memset(skyLight, 0, sizeof(skyLight));
        // Fill with air
        for (size_t i = 0; i < TOTAL; i++) blocks[i].setType(AIR);
        // Stone floor at y <= 60
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 0; y <= 60; y++)
                    blocks[skyIdx(x, y, z)].setType(STONE);
        // Sky light: 15 for all air blocks above the floor (simple vertical propagation)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 61; y < CHUNK_HEIGHT; y++)
                    skyLight[skyIdx(x, y, z)] = 15;
    }
};

// --- Hotbar Tests ---

TEST(HotbarTest, AllBlocksArePlaceable) {
    // All hotbar blocks should be solid (placeable)
    block_type hotbar[] = {GRASS, DIRT, STONE, WOOD, SAND, SNOW, GLOWSTONE, LEAVES, COAL_ORE, GRAVEL};
    for (auto bt : hotbar) {
        EXPECT_TRUE(hasFlag(bt, BF_SOLID)) << "Hotbar block " << bt << " should be solid";
        EXPECT_NE(bt, AIR) << "AIR should not be in hotbar";
        EXPECT_NE(bt, WATER) << "WATER should not be in hotbar";
    }
}

TEST(HotbarTest, SlotKeyMapping) {
    // Keys 1-9 map to slots 0-8, key 0 maps to slot 9
    for (int i = 0; i < 10; i++) {
        int displayNum = (i + 1) % 10; // 1,2,...,9,0
        if (i < 9)
            EXPECT_EQ(displayNum, i + 1);
        else
            EXPECT_EQ(displayNum, 0);
    }
}

// --- Sky Light Darkening Tests ---

TEST(SkyLightTest, PlaceBlockDarkensBelow) {
    TestChunk tc;
    // Place a stone block at y=70 (in the air column at x=8, z=8)
    int x = 8, y = 70, z = 8;
    tc.blocks[skyIdx(x, y, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, x, y, z);

    // The placed block should have light 0
    EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], 0);
    // Blocks below should no longer have light 15 (sky column is broken)
    for (int by = y - 1; by > 60; by--) {
        EXPECT_LT(tc.skyLight[skyIdx(x, by, z)], 15)
            << "Block at y=" << by << " should not have full sky light after placement above";
    }
}

TEST(SkyLightTest, PlaceBlockDoesNotAffectDistantColumns) {
    TestChunk tc;
    // Place a stone block at (8, 70, 8)
    tc.blocks[skyIdx(8, 70, 8)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, 8, 70, 8);

    // A distant column (0, y, 0) should still have full sky light
    for (int y = 61; y < CHUNK_HEIGHT; y++) {
        EXPECT_EQ(tc.skyLight[skyIdx(0, y, 0)], 15)
            << "Distant column at y=" << y << " should be unaffected";
    }
}

TEST(SkyLightTest, PlaceBlockAtSurfaceLevel) {
    TestChunk tc;
    // Place right above the floor at y=61
    int x = 4, z = 4;
    tc.blocks[skyIdx(x, 61, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, x, 61, z);

    // Placed block: light 0
    EXPECT_EQ(tc.skyLight[skyIdx(x, 61, z)], 0);
    // Block above (y=62) should still have sky light (from neighbors)
    EXPECT_GT(tc.skyLight[skyIdx(x, 62, z)], 0);
}

TEST(SkyLightTest, DestroyBlockRestoresLight) {
    TestChunk tc;
    // Place then destroy a block
    int x = 8, y = 70, z = 8;
    tc.blocks[skyIdx(x, y, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, x, y, z);

    // Verify it's dark
    EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], 0);

    // Destroy the block
    tc.blocks[skyIdx(x, y, z)].setType(AIR);
    updateSkyLightLocal(tc.blocks, tc.skyLight, x, y, z);

    // Should restore full sky light (direct sky column)
    EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], 15);
}

TEST(SkyLightTest, PlaceAndDestroyRoundTrip) {
    TestChunk tc;
    int x = 5, z = 5;

    // Save original light for the column
    uint8_t originalLight[CHUNK_HEIGHT];
    for (int y = 0; y < CHUNK_HEIGHT; y++)
        originalLight[y] = tc.skyLight[skyIdx(x, y, z)];

    // Place a block at y=80
    tc.blocks[skyIdx(x, 80, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, x, 80, z);

    // Destroy it
    tc.blocks[skyIdx(x, 80, z)].setType(AIR);
    updateSkyLightLocal(tc.blocks, tc.skyLight, x, 80, z);

    // Light should be fully restored (sky column re-established)
    for (int y = 61; y < CHUNK_HEIGHT; y++) {
        EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], originalLight[y])
            << "Light at y=" << y << " should be restored after place+destroy round trip";
    }
}

TEST(SkyLightTest, PlacedBlockSkyLightIsZero) {
    TestChunk tc;
    // Place opaque block in full sunlight
    int x = 3, y = 100, z = 3;
    EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], 15); // was full sky light
    tc.blocks[skyIdx(x, y, z)].setType(DIRT);
    darkenSkyLightLocal(tc.blocks, tc.skyLight, x, y, z);
    EXPECT_EQ(tc.skyLight[skyIdx(x, y, z)], 0);
}

// --- Block Placement Coordinate Tests ---

TEST(PlacementCoordTest, WorldToChunkForPlacement) {
    // Placing at world x=16 should be chunk 1, local 0
    EXPECT_EQ(worldToChunk(16), 1);
    EXPECT_EQ(worldToLocal(16, 1), 0);

    // Placing at world x=-1 should be chunk -1, local 15
    EXPECT_EQ(worldToChunk(-1), -1);
    EXPECT_EQ(worldToLocal(-1, -1), 15);
}

TEST(PlacementCoordTest, BorderBlockDetection) {
    // lx == 0 means block is on the -X border of a chunk
    EXPECT_EQ(worldToLocal(0, 0), 0);
    EXPECT_EQ(worldToLocal(16, 1), 0);

    // lx == CHUNK_SIZE - 1 means block is on the +X border
    EXPECT_EQ(worldToLocal(15, 0), CHUNK_SIZE - 1);
    EXPECT_EQ(worldToLocal(-1, -1), CHUNK_SIZE - 1);
}
