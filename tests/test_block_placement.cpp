#include <gtest/gtest.h>
#include "cube.h"
#include "light_data.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Replicate the sky light index function used in chunk.cpp
static size_t skyIdx(int x, int y, int z) {
    return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
}

// Replicate darkenSkyLightLocal for testing (same algorithm as chunk.cpp, packed format)
static void darkenSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    { size_t i = skyIdx(x, y, z); skyLight[i] = skyLight[i] & 0xF; }

    struct Node {
        int x, y, z;
        uint8_t oldLight;
    };
    std::vector<Node> removeQueue;
    removeQueue.reserve(256);

    for (int by = y - 1; by >= 0; by--) {
        if (hasFlag(blocks[skyIdx(x, by, z)].getType(), BF_OPAQUE)) break;
        size_t bi = skyIdx(x, by, z);
        if (unpackSky(skyLight[bi]) != 15) break;
        uint8_t old = unpackSky(skyLight[bi]);
        skyLight[bi] = skyLight[bi] & 0xF;
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
            size_t ni = skyIdx(nx, ny, nz);
            uint8_t neighborLight = unpackSky(skyLight[ni]);
            if (neighborLight > 0 && neighborLight < oldLight) {
                skyLight[ni] = skyLight[ni] & 0xF;
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
        uint8_t light = unpackSky(skyLight[skyIdx(cx, cy, cz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            size_t ni = skyIdx(nx, ny, nz);
            if (unpackSky(skyLight[ni]) >= propagated) continue;
            skyLight[ni] = (propagated << 4) | (skyLight[ni] & 0xF);
            lightQueue.push_back({nx, ny, nz});
        }
    }
}

// Replicate updateSkyLightLocal (brightening after block removal, packed format)
static void updateSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    uint8_t maxLight = 0;
    for (auto& d : DIRS) {
        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_HEIGHT && nz >= 0 && nz < CHUNK_SIZE) {
            uint8_t nl = unpackSky(skyLight[skyIdx(nx, ny, nz)]);
            if (nl > maxLight) maxLight = nl;
        }
    }
    if (y + 1 >= CHUNK_HEIGHT) maxLight = 15;

    uint8_t newLight =
        (y + 1 < CHUNK_HEIGHT && unpackSky(skyLight[skyIdx(x, y + 1, z)]) == 15) ? 15 : (maxLight > 0 ? maxLight - 1 : 0);
    { size_t i = skyIdx(x, y, z); skyLight[i] = (newLight << 4) | (skyLight[i] & 0xF); }

    struct Node {
        int x, y, z;
    };
    std::vector<Node> queue;
    queue.reserve(256);
    queue.push_back({x, y, z});

    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        uint8_t light = unpackSky(skyLight[skyIdx(cx, cy, cz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            size_t ni = skyIdx(nx, ny, nz);
            if (unpackSky(skyLight[ni]) >= propagated) continue;
            skyLight[ni] = (propagated << 4) | (skyLight[ni] & 0xF);
            queue.push_back({nx, ny, nz});
        }
    }
}

// Helper: create a flat world -- stone floor at y=60, air above
// Uses packed light format: high nibble = sky, low nibble = block
struct TestChunk {
    static constexpr size_t TOTAL = CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
    Cube blocks[TOTAL];
    uint8_t light[TOTAL]; // packed: high nibble = sky, low nibble = block

    TestChunk() {
        memset(light, 0, sizeof(light));
        // Fill with air
        for (size_t i = 0; i < TOTAL; i++) blocks[i].setType(AIR);
        // Stone floor at y <= 60
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 0; y <= 60; y++)
                    blocks[skyIdx(x, y, z)].setType(STONE);
        // Sky light: 15 for all air blocks above the floor (packed into high nibble)
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 61; y < CHUNK_HEIGHT; y++)
                    light[skyIdx(x, y, z)] = 15 << 4;
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
    darkenSkyLightLocal(tc.blocks, tc.light, x, y, z);

    // The placed block should have light 0
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), 0);
    // Blocks below should no longer have light 15 (sky column is broken)
    for (int by = y - 1; by > 60; by--) {
        EXPECT_LT(unpackSky(tc.light[skyIdx(x, by, z)]), 15)
            << "Block at y=" << by << " should not have full sky light after placement above";
    }
}

TEST(SkyLightTest, PlaceBlockDoesNotAffectDistantColumns) {
    TestChunk tc;
    // Place a stone block at (8, 70, 8)
    tc.blocks[skyIdx(8, 70, 8)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.light, 8, 70, 8);

    // A distant column (0, y, 0) should still have full sky light
    for (int y = 61; y < CHUNK_HEIGHT; y++) {
        EXPECT_EQ(unpackSky(tc.light[skyIdx(0, y, 0)]), 15)
            << "Distant column at y=" << y << " should be unaffected";
    }
}

TEST(SkyLightTest, PlaceBlockAtSurfaceLevel) {
    TestChunk tc;
    // Place right above the floor at y=61
    int x = 4, z = 4;
    tc.blocks[skyIdx(x, 61, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.light, x, 61, z);

    // Placed block: light 0
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, 61, z)]), 0);
    // Block above (y=62) should still have sky light (from neighbors)
    EXPECT_GT(unpackSky(tc.light[skyIdx(x, 62, z)]), 0);
}

TEST(SkyLightTest, DestroyBlockRestoresLight) {
    TestChunk tc;
    // Place then destroy a block
    int x = 8, y = 70, z = 8;
    tc.blocks[skyIdx(x, y, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.light, x, y, z);

    // Verify it's dark
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), 0);

    // Destroy the block
    tc.blocks[skyIdx(x, y, z)].setType(AIR);
    updateSkyLightLocal(tc.blocks, tc.light, x, y, z);

    // Should restore full sky light (direct sky column)
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), 15);
}

TEST(SkyLightTest, PlaceAndDestroyRoundTrip) {
    TestChunk tc;
    int x = 5, z = 5;

    // Save original light for the column
    uint8_t originalLight[CHUNK_HEIGHT];
    for (int y = 0; y < CHUNK_HEIGHT; y++)
        originalLight[y] = unpackSky(tc.light[skyIdx(x, y, z)]);

    // Place a block at y=80
    tc.blocks[skyIdx(x, 80, z)].setType(STONE);
    darkenSkyLightLocal(tc.blocks, tc.light, x, 80, z);

    // Destroy it
    tc.blocks[skyIdx(x, 80, z)].setType(AIR);
    updateSkyLightLocal(tc.blocks, tc.light, x, 80, z);

    // Light should be fully restored (sky column re-established)
    for (int y = 61; y < CHUNK_HEIGHT; y++) {
        EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), originalLight[y])
            << "Light at y=" << y << " should be restored after place+destroy round trip";
    }
}

TEST(SkyLightTest, PlacedBlockSkyLightIsZero) {
    TestChunk tc;
    // Place opaque block in full sunlight
    int x = 3, y = 100, z = 3;
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), 15); // was full sky light
    tc.blocks[skyIdx(x, y, z)].setType(DIRT);
    darkenSkyLightLocal(tc.blocks, tc.light, x, y, z);
    EXPECT_EQ(unpackSky(tc.light[skyIdx(x, y, z)]), 0);
}

// --- Block Light Tests ---

// Replicate computeBlockLightData BFS using packed format (low nibble)
static void computeBlockLight(Cube* blocks, uint8_t* packedLight) {
    const size_t total = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
    // Zero only the block light nibble (preserve sky light)
    for (size_t i = 0; i < total; i++) packedLight[i] = packedLight[i] & 0xF0;
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    struct Node { int x, y, z; };
    std::vector<Node> queue;
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < CHUNK_HEIGHT; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                uint8_t em = getBlockLightEmission(blocks[skyIdx(x, y, z)].getType());
                if (em > 0) {
                    size_t i = skyIdx(x, y, z);
                    packedLight[i] = (packedLight[i] & 0xF0) | (em & 0xF);
                    queue.push_back({x, y, z});
                }
            }
    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        uint8_t light = unpackBlock(packedLight[skyIdx(cx, cy, cz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t newLight = light - 1;
            size_t ni = skyIdx(nx, ny, nz);
            if (unpackBlock(packedLight[ni]) >= newLight) continue;
            packedLight[ni] = (packedLight[ni] & 0xF0) | (newLight & 0xF);
            queue.push_back({nx, ny, nz});
        }
    }
}

// Simulate cross-chunk border propagation using packed format
static void propagateBorderBlockLight(Cube* blocks, uint8_t* packedLight, const uint8_t* neighborBorder, int borderX) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    struct Node { int x, y, z; };
    std::vector<Node> queue;
    for (int y = 0; y < CHUNK_HEIGHT; y++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint8_t nLight = neighborBorder[y * CHUNK_SIZE + z];
            size_t bi = skyIdx(borderX, y, z);
            if (nLight > 1 && nLight - 1 > unpackBlock(packedLight[bi]) &&
                !hasFlag(blocks[bi].getType(), BF_OPAQUE)) {
                packedLight[bi] = (packedLight[bi] & 0xF0) | ((nLight - 1) & 0xF);
                queue.push_back({borderX, y, z});
            }
        }
    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        uint8_t light = unpackBlock(packedLight[skyIdx(cx, cy, cz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[skyIdx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            uint8_t newLight = light - 1;
            size_t ni = skyIdx(nx, ny, nz);
            if (unpackBlock(packedLight[ni]) >= newLight) continue;
            packedLight[ni] = (packedLight[ni] & 0xF0) | (newLight & 0xF);
            queue.push_back({nx, ny, nz});
        }
    }
}

TEST(BlockLightTest, GlowstoneEmitsLight) {
    TestChunk tc;
    // Place glowstone at (8, 65, 8)
    tc.blocks[skyIdx(8, 65, 8)].setType(GLOWSTONE);
    computeBlockLight(tc.blocks, tc.light);

    EXPECT_EQ(unpackBlock(tc.light[skyIdx(8, 65, 8)]), 15);
    // Adjacent blocks get 14
    EXPECT_EQ(unpackBlock(tc.light[skyIdx(9, 65, 8)]), 14);
    EXPECT_EQ(unpackBlock(tc.light[skyIdx(8, 66, 8)]), 14);
    // Two away gets 13
    EXPECT_EQ(unpackBlock(tc.light[skyIdx(10, 65, 8)]), 13);
}

TEST(BlockLightTest, LightBlockedByOpaque) {
    TestChunk tc;
    // Place glowstone at (8, 65, 8) and a stone wall next to it
    tc.blocks[skyIdx(8, 65, 8)].setType(GLOWSTONE);
    tc.blocks[skyIdx(9, 65, 8)].setType(STONE);
    computeBlockLight(tc.blocks, tc.light);

    EXPECT_EQ(unpackBlock(tc.light[skyIdx(8, 65, 8)]), 15);
    // Stone blocks light
    EXPECT_EQ(unpackBlock(tc.light[skyIdx(9, 65, 8)]), 0);
    // Behind the wall: light must go around (at least 3 steps away), so much dimmer
    EXPECT_LT(unpackBlock(tc.light[skyIdx(10, 65, 8)]), 13);
}

TEST(BlockLightTest, LightAttenuatesUniformly) {
    TestChunk tc;
    tc.blocks[skyIdx(8, 65, 8)].setType(GLOWSTONE);
    computeBlockLight(tc.blocks, tc.light);

    // Light decreases by 1 per block in any direction
    for (int dist = 1; dist <= 7; dist++) {
        // Check along +X
        if (8 + dist < CHUNK_SIZE) {
            EXPECT_EQ(unpackBlock(tc.light[skyIdx(8 + dist, 65, 8)]), 15 - dist)
                << "Light at distance " << dist << " should be " << (15 - dist);
        }
    }
}

TEST(BlockLightTest, CrossChunkPropagation) {
    // Simulate two adjacent chunks: chunkA (left) has glowstone at x=15, chunkB (right) should receive light

    // ChunkA: glowstone at (15, 65, 8)
    TestChunk chunkA;
    chunkA.blocks[skyIdx(15, 65, 8)].setType(GLOWSTONE);
    computeBlockLight(chunkA.blocks, chunkA.light);

    // ChunkA's x=15 border should have light
    EXPECT_EQ(unpackBlock(chunkA.light[skyIdx(15, 65, 8)]), 15);

    // ChunkB: empty air above floor, no emissive blocks
    TestChunk chunkB;
    computeBlockLight(chunkB.blocks, chunkB.light);
    EXPECT_EQ(unpackBlock(chunkB.light[skyIdx(0, 65, 8)]), 0); // no light yet

    // Extract chunkA's x=15 border block light as neighbor data for chunkB
    uint8_t borderFromA[CHUNK_HEIGHT * CHUNK_SIZE];
    for (int y = 0; y < CHUNK_HEIGHT; y++)
        for (int z = 0; z < CHUNK_SIZE; z++)
            borderFromA[y * CHUNK_SIZE + z] = unpackBlock(chunkA.light[skyIdx(15, y, z)]);

    // Propagate from A's border into B's x=0
    propagateBorderBlockLight(chunkB.blocks, chunkB.light, borderFromA, 0);

    // ChunkB's x=0 should now have light from the glowstone (15 - 1 = 14)
    EXPECT_EQ(unpackBlock(chunkB.light[skyIdx(0, 65, 8)]), 14)
        << "Cross-chunk propagation: block at x=0 should receive light 14 from neighbor's glowstone at x=15";

    // Light should continue propagating inward
    EXPECT_EQ(unpackBlock(chunkB.light[skyIdx(1, 65, 8)]), 13);
    EXPECT_EQ(unpackBlock(chunkB.light[skyIdx(2, 65, 8)]), 12);

    // Far away should have no light (14 steps in = 0)
    EXPECT_EQ(unpackBlock(chunkB.light[skyIdx(14, 65, 8)]), 0);
}

TEST(BlockLightTest, WorldSpaceBFSCrossesMultipleChunks) {
    // Simulate world-space BFS: glowstone at chunk A's corner (x=15, z=15)
    // Light should reach into chunk B (+X), chunk C (+Z), and chunk D (+X,+Z diagonal)
    // We simulate 4 chunks as separate arrays, using world-to-local conversion

    static constexpr size_t TOTAL = CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
    // Chunk (0,0), (1,0), (0,1), (1,1)
    Cube blocksA[TOTAL], blocksB[TOTAL], blocksC[TOTAL], blocksD[TOTAL];
    uint8_t lightA[TOTAL], lightB[TOTAL], lightC[TOTAL], lightD[TOTAL];
    memset(lightA, 0, TOTAL); memset(lightB, 0, TOTAL);
    memset(lightC, 0, TOTAL); memset(lightD, 0, TOTAL);

    // All air except floor at y<=60
    auto initChunk = [](Cube* blocks) {
        for (size_t i = 0; i < TOTAL; i++) blocks[i].setType(AIR);
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                for (int y = 0; y <= 60; y++)
                    blocks[skyIdx(x, y, z)].setType(STONE);
    };
    initChunk(blocksA); initChunk(blocksB); initChunk(blocksC); initChunk(blocksD);

    // World-space accessor helpers
    auto getChunkData = [&](int wx, int wz) -> std::pair<Cube*, uint8_t*> {
        int cx = wx / CHUNK_SIZE, cz = wz / CHUNK_SIZE;
        if (wx < 0 || wz < 0) return {nullptr, nullptr}; // out of our 4-chunk grid
        if (cx == 0 && cz == 0) return {blocksA, lightA};
        if (cx == 1 && cz == 0) return {blocksB, lightB};
        if (cx == 0 && cz == 1) return {blocksC, lightC};
        if (cx == 1 && cz == 1) return {blocksD, lightD};
        return {nullptr, nullptr};
    };

    // World-space BFS (same algorithm as floodBlockLight in world.cpp, using packed low nibble)
    struct Node { int x, y, z; };
    std::vector<Node> queue;
    int sx = 15, sy = 65, sz = 15; // world coords: chunk A corner

    // Place glowstone
    blocksA[skyIdx(15, 65, 15)].setType(GLOWSTONE);
    { size_t i = skyIdx(15, 65, 15); lightA[i] = (lightA[i] & 0xF0) | 15; }
    queue.push_back({sx, sy, sz});

    static const int DIRS[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        auto [cblocks, clight] = getChunkData(cx, cz);
        if (!cblocks) continue;
        int lx = cx % CHUNK_SIZE, lz = cz % CHUNK_SIZE;
        uint8_t light = unpackBlock(clight[skyIdx(lx, cy, lz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
            auto [nblocks, nlight] = getChunkData(nx, nz);
            if (!nblocks) continue;
            int nlx = nx % CHUNK_SIZE, nlz = nz % CHUNK_SIZE;
            if (hasFlag(nblocks[skyIdx(nlx, ny, nlz)].getType(), BF_OPAQUE)) continue;
            uint8_t propagated = light - 1;
            size_t ni = skyIdx(nlx, ny, nlz);
            if (unpackBlock(nlight[ni]) >= propagated) continue;
            nlight[ni] = (nlight[ni] & 0xF0) | (propagated & 0xF);
            queue.push_back({nx, ny, nz});
        }
    }

    // Verify: chunk A source
    EXPECT_EQ(unpackBlock(lightA[skyIdx(15, 65, 15)]), 15);
    EXPECT_EQ(unpackBlock(lightA[skyIdx(14, 65, 15)]), 14);

    // Verify: chunk B (x=16 in world = local x=0 in chunk 1,0)
    EXPECT_EQ(unpackBlock(lightB[skyIdx(0, 65, 15)]), 14) << "Light should cross +X chunk border";
    EXPECT_EQ(unpackBlock(lightB[skyIdx(1, 65, 15)]), 13) << "Light should continue into chunk B";

    // Verify: chunk C (z=16 in world = local z=0 in chunk 0,1)
    EXPECT_EQ(unpackBlock(lightC[skyIdx(15, 65, 0)]), 14) << "Light should cross +Z chunk border";

    // Verify: chunk D (diagonal, x=16 z=16 = local 0,0 in chunk 1,1)
    // Diagonal takes 2 steps (via B or C), so light = 13
    EXPECT_EQ(unpackBlock(lightD[skyIdx(0, 65, 0)]), 13) << "Light should reach diagonal chunk via 2 border crossings";

    // Verify: light attenuates to 0 far into chunk B
    EXPECT_EQ(unpackBlock(lightB[skyIdx(14, 65, 15)]), 0) << "Light should fully attenuate 15 blocks from source";
}

TEST(BlockLightTest, NoEmissionForNonEmissiveBlocks) {
    EXPECT_EQ(getBlockLightEmission(AIR), 0);
    EXPECT_EQ(getBlockLightEmission(STONE), 0);
    EXPECT_EQ(getBlockLightEmission(DIRT), 0);
    EXPECT_EQ(getBlockLightEmission(GRASS), 0);
    EXPECT_EQ(getBlockLightEmission(GLOWSTONE), 15);
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
