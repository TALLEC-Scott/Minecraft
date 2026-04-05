#include <gtest/gtest.h>
#include "cube.h"
#include <cmath>

// Test the floor-division coordinate conversion used everywhere
static int worldToChunk(int coord) {
    return (coord >= 0) ? coord / CHUNK_SIZE : (coord - CHUNK_SIZE + 1) / CHUNK_SIZE;
}

static int worldToLocal(int coord, int chunkCoord) {
    return coord - chunkCoord * CHUNK_SIZE;
}

TEST(CoordinateTest, PositiveCoordinates) {
    EXPECT_EQ(worldToChunk(0), 0);
    EXPECT_EQ(worldToChunk(15), 0);
    EXPECT_EQ(worldToChunk(16), 1);
    EXPECT_EQ(worldToChunk(31), 1);
    EXPECT_EQ(worldToChunk(32), 2);
}

TEST(CoordinateTest, NegativeCoordinates) {
    EXPECT_EQ(worldToChunk(-1), -1);
    EXPECT_EQ(worldToChunk(-16), -1);
    EXPECT_EQ(worldToChunk(-17), -2);
    EXPECT_EQ(worldToChunk(-32), -2);
    EXPECT_EQ(worldToChunk(-33), -3);
}

TEST(CoordinateTest, LocalCoordsAlwaysPositive) {
    for (int x = -100; x <= 100; x++) {
        int cx = worldToChunk(x);
        int lx = worldToLocal(x, cx);
        EXPECT_GE(lx, 0) << "world=" << x << " chunk=" << cx;
        EXPECT_LT(lx, CHUNK_SIZE) << "world=" << x << " chunk=" << cx;
    }
}

TEST(CoordinateTest, RoundTrip) {
    for (int x = -100; x <= 100; x++) {
        int cx = worldToChunk(x);
        int lx = worldToLocal(x, cx);
        int reconstructed = cx * CHUNK_SIZE + lx;
        EXPECT_EQ(reconstructed, x);
    }
}

TEST(CoordinateTest, ChunkBoundaries) {
    // Block at x=0 should be in chunk 0, local 0
    EXPECT_EQ(worldToChunk(0), 0);
    EXPECT_EQ(worldToLocal(0, 0), 0);

    // Block at x=-1 should be in chunk -1, local 15
    EXPECT_EQ(worldToChunk(-1), -1);
    EXPECT_EQ(worldToLocal(-1, -1), 15);

    // Block at x=16 should be in chunk 1, local 0
    EXPECT_EQ(worldToChunk(16), 1);
    EXPECT_EQ(worldToLocal(16, 1), 0);
}
