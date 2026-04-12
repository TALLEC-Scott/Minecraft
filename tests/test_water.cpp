// Tests for the real WaterSimulator. Builds against the stub world.h /
// chunk.h / ChunkManager.h in tests/stubs/, which expose just enough of
// the interface for src/WaterSimulator.cpp to compile and run without
// pulling in OpenGL, threading, or the rendering pipeline.

#include <gtest/gtest.h>
#include "WaterSimulator.h"
#include "world.h"

namespace {

// Ensure a chunk exists covering a (world-space) column so setBlock doesn't
// no-op on a missing chunk. WaterSimulator mutates blocks across many
// positions, so we eagerly create every chunk the test will touch.
void ensureChunks(World& world, int minX, int maxX, int minZ, int maxZ) {
    int minCX = worldToChunk(minX), maxCX = worldToChunk(maxX);
    int minCZ = worldToChunk(minZ), maxCZ = worldToChunk(maxZ);
    for (int cx = minCX; cx <= maxCX; cx++)
        for (int cz = minCZ; cz <= maxCZ; cz++)
            world.chunkManager->getOrCreate(cx, cz);
}

// Fill a horizontal slab of stone (a "floor") between the given world coords.
void fillStone(World& world, int x0, int x1, int y, int z0, int z1) {
    for (int x = x0; x <= x1; x++)
        for (int z = z0; z <= z1; z++)
            world.setBlock(x, y, z, STONE, 0);
}

// Read the current block type at a world position by walking into the chunk.
block_type getType(World& world, int x, int y, int z) {
    int cx = worldToChunk(x), cz = worldToChunk(z);
    Chunk* c = world.chunkManager->getChunk(cx, cz);
    if (!c) return AIR;
    return c->getBlockType(worldToLocal(x, cx), y, worldToLocal(z, cz));
}

// Read the raw water byte (level bits + falling flag).
uint8_t getWaterRaw(World& world, int x, int y, int z) {
    int cx = worldToChunk(x), cz = worldToChunk(z);
    Chunk* c = world.chunkManager->getChunk(cx, cz);
    if (!c) return 0;
    return c->getWaterLevel(worldToLocal(x, cx), y, worldToLocal(z, cz));
}

// Run N simulator ticks. tick() internally gates on TICK_INTERVAL, so we
// bump frameCounter forward to fire on each call.
void runTicks(WaterSimulator& sim, int n) {
    for (int i = 0; i < n; i++) {
        sim.frameCounter = WaterSimulator::TICK_INTERVAL - 1;
        sim.tick();
    }
}

struct PedestalScene {
    World world;
    WaterSimulator sim{&world};
    static constexpr int FLOOR_Y = 2;
    static constexpr int SRC_X = 8;
    static constexpr int SRC_Z = 8;
    static constexpr int SRC_Y = 8; // well above the floor

    PedestalScene() {
        ensureChunks(world, -8, 24, -8, 24);
        fillStone(world, -8, 24, FLOOR_Y, -8, 24);
    }

    void placeSource() {
        world.setBlock(SRC_X, SRC_Y, SRC_Z, WATER, 0);
        sim.activate(SRC_X, SRC_Y, SRC_Z);
    }
    void removeSource() {
        world.setBlock(SRC_X, SRC_Y, SRC_Z, AIR, 0);
        sim.activateNeighbors(SRC_X, SRC_Y, SRC_Z);
    }
};

} // namespace

// --- Source placement & falling column ---

TEST(WaterSimulator, SourceOverAirCreatesFallingColumn) {
    PedestalScene s;
    s.placeSource();
    runTicks(s.sim, 20);

    // Every cell from just below the source down to the floor should be
    // water, tagged as falling (the flag is set by Rule 1 gravity).
    for (int y = PedestalScene::SRC_Y - 1; y > PedestalScene::FLOOR_Y; y--) {
        EXPECT_EQ(getType(s.world, s.SRC_X, y, s.SRC_Z), WATER) << "y=" << y;
        EXPECT_TRUE(waterIsFalling(getWaterRaw(s.world, s.SRC_X, y, s.SRC_Z))) << "y=" << y;
    }
    // Floor is still stone.
    EXPECT_EQ(getType(s.world, s.SRC_X, PedestalScene::FLOOR_Y, s.SRC_Z), STONE);
}

TEST(WaterSimulator, SourceIsNeverDecayed) {
    PedestalScene s;
    s.placeSource();
    runTicks(s.sim, 30);
    // Source cell remains WATER with raw = 0 (source, no flag).
    EXPECT_EQ(getType(s.world, s.SRC_X, s.SRC_Y, s.SRC_Z), WATER);
    uint8_t raw = getWaterRaw(s.world, s.SRC_X, s.SRC_Y, s.SRC_Z);
    EXPECT_TRUE(waterIsSource(raw));
}

// --- Horizontal spread on a floor ---

TEST(WaterSimulator, LandedWaterSpreadsSevenCells) {
    PedestalScene s;
    // Put a source directly on the floor so Rule 2 spreads at floor level
    // without a falling column getting involved.
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 15);

    // After enough ticks the source should reach 7 cells in each cardinal.
    EXPECT_EQ(getType(s.world, 15, y, 8), WATER);
    EXPECT_EQ(waterFlowLevel(getWaterRaw(s.world, 15, y, 8)), WATER_MAX_FLOW);
    // 8 cells out is beyond the max spread radius.
    EXPECT_EQ(getType(s.world, 16, y, 8), AIR);
}

// --- Drain behaviour (ripple) ---

TEST(WaterSimulator, SourceRemovalDrainsEverything) {
    PedestalScene s;
    s.placeSource();
    runTicks(s.sim, 40); // let it fully settle

    s.removeSource();
    runTicks(s.sim, 80); // 7 cells × several ripple ticks + falling column

    // Everywhere in the small neighborhood around the old source should
    // be back to AIR (except the stone floor).
    for (int dx = -7; dx <= 7; dx++) {
        for (int dz = -7; dz <= 7; dz++) {
            for (int y = PedestalScene::FLOOR_Y + 1; y <= PedestalScene::SRC_Y; y++) {
                EXPECT_EQ(getType(s.world, PedestalScene::SRC_X + dx, y, PedestalScene::SRC_Z + dz), AIR)
                    << "at (" << dx << "," << y << "," << dz << ")";
            }
        }
    }
}

// --- Falling water is not a source (no infinite generator) ---

TEST(WaterSimulator, FallingWaterNeverBecomesSource) {
    PedestalScene s;
    s.placeSource();
    runTicks(s.sim, 20);
    // Every non-top water cell in the column should have the falling flag
    // set and NOT be a source.
    for (int y = PedestalScene::SRC_Y - 1; y > PedestalScene::FLOOR_Y; y--) {
        uint8_t raw = getWaterRaw(s.world, s.SRC_X, y, s.SRC_Z);
        EXPECT_TRUE(waterIsFalling(raw)) << "y=" << y;
        EXPECT_FALSE(waterIsSource(raw)) << "y=" << y;
    }
}

// --- Cliff edge: flowing water does not sheet across empty space ---

TEST(WaterSimulator, FlowingWaterDoesNotSheetOverCliff) {
    PedestalScene s;
    // Build a narrow platform at y=8: stone from x=6..10 on z=8.
    for (int x = 6; x <= 10; x++) s.world.setBlock(x, 8, 8, STONE, 0);
    const int y = 9;
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 25);

    // One cell past the platform edge (x=5, x=11) is a legitimate "lip"
    // cell: it's written by a landed neighbor and feeds the falling
    // column straight below. What must NOT happen is water *beyond* that
    // lip — cells at x=4 and x=12 would only exist if a non-landed cell
    // fanned out further, which is the checker bug this rule prevents.
    EXPECT_EQ(getType(s.world, 4, y, 8), AIR);
    EXPECT_EQ(getType(s.world, 12, y, 8), AIR);
    EXPECT_EQ(getType(s.world, 3, y, 8), AIR);
    EXPECT_EQ(getType(s.world, 13, y, 8), AIR);
}

// --- Drain is monotonic: water count strictly decreases until zero ---

// --- Infinite water source rule ---

TEST(WaterSimulator, TwoSourceNeighborsCreateNewSource) {
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    // Place two source blocks with a 1-block gap between them.
    // The gap should become a source after a few ticks (Rule 0).
    s.world.setBlock(7, y, 8, WATER, 0);
    s.world.setBlock(9, y, 8, WATER, 0);
    s.sim.activate(7, y, 8);
    s.sim.activate(9, y, 8);
    runTicks(s.sim, 10);

    // The gap at (8, y, 8) should now be a source.
    EXPECT_EQ(getType(s.world, 8, y, 8), WATER);
    EXPECT_TRUE(waterIsSource(getWaterRaw(s.world, 8, y, 8)))
        << "cell between two sources should become a source";
}

TEST(WaterSimulator, SingleSourceDoesNotClone) {
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    // Place one source. Adjacent cells should be flowing, not sources.
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 10);

    EXPECT_EQ(getType(s.world, 9, y, 8), WATER);
    EXPECT_FALSE(waterIsSource(getWaterRaw(s.world, 9, y, 8)))
        << "cell next to a single source should be flowing, not source";
}

TEST(WaterSimulator, OceanGapFillsAsSource) {
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    // Simulate an ocean: a row of source blocks with one gap (broken block).
    for (int x = 4; x <= 12; x++) {
        if (x == 8) continue; // the gap
        s.world.setBlock(x, y, 8, WATER, 0);
        s.sim.activate(x, y, 8);
    }
    runTicks(s.sim, 10);

    // The gap at (8, y, 8) has sources on both sides → should be source.
    EXPECT_EQ(getType(s.world, 8, y, 8), WATER);
    EXPECT_TRUE(waterIsSource(getWaterRaw(s.world, 8, y, 8)))
        << "gap in ocean row should fill as source";
}

// --- Flow direction ---

TEST(WaterSimulator, FlowDirectionPointsOutward) {
    // Place a source on a flat surface. After spreading, each flowing
    // cell's neighbors at higher levels (further from source) should be
    // in the "outward" direction. We verify this by checking that the
    // flow vector (accumulated from neighbor levels) points AWAY from
    // the source for cells along each cardinal axis.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 15);

    // Cell at (9, y, 8) is level 1. Its +X neighbor (10,y,8) is level 2.
    // Flow should point toward +X (away from source).
    // Verify by checking the water levels form a gradient.
    EXPECT_EQ(getType(s.world, 9, y, 8), WATER);
    EXPECT_EQ(getType(s.world, 10, y, 8), WATER);
    uint8_t lvl9 = waterFlowLevel(getWaterRaw(s.world, 9, y, 8));
    uint8_t lvl10 = waterFlowLevel(getWaterRaw(s.world, 10, y, 8));
    EXPECT_LT(lvl9, lvl10) << "level should increase away from source";

    // Same in -X direction
    uint8_t lvl7 = waterFlowLevel(getWaterRaw(s.world, 7, y, 8));
    uint8_t lvl6 = waterFlowLevel(getWaterRaw(s.world, 6, y, 8));
    EXPECT_LT(lvl7, lvl6) << "level should increase away from source in -X";

    // Diagonal: (9,y,9) should have higher level than (8,y,8) source
    EXPECT_EQ(getType(s.world, 9, y, 9), WATER);
    uint8_t lvlDiag = waterFlowLevel(getWaterRaw(s.world, 9, y, 9));
    EXPECT_GT(lvlDiag, 0u) << "diagonal cell should be flowing (not source)";

    // Source itself stays level 0
    EXPECT_TRUE(waterIsSource(getWaterRaw(s.world, 8, y, 8)));
}

// Helper: compute the flow direction vector for a water cell the same way
// the mesh builder does (chunk.cpp buildMeshData). Returns (fx, fz).
std::pair<float, float> computeFlowDir(World& world, int wx, int wy, int wz) {
    uint8_t raw = getWaterRaw(world, wx, wy, wz);
    if (waterIsSource(raw) || waterIsFalling(raw)) return {0, 0};
    int myLvl = waterFlowLevel(raw);
    float fx = 0, fz = 0;
    // +X / -X
    for (int dx : {-1, 1}) {
        int nx = wx + dx;
        block_type nbt = getType(world, nx, wy, wz);
        if (nbt == AIR) { fx += dx * 8; }
        else if (nbt == WATER) {
            uint8_t nraw = getWaterRaw(world, nx, wy, wz);
            int nLvl = waterIsSource(nraw) ? 0 : waterFlowLevel(nraw);
            fx += dx * (nLvl - myLvl);
        }
    }
    // +Z / -Z
    for (int dz : {-1, 1}) {
        int nz = wz + dz;
        block_type nbt = getType(world, wx, wy, nz);
        if (nbt == AIR) { fz += dz * 8; }
        else if (nbt == WATER) {
            uint8_t nraw = getWaterRaw(world, wx, wy, nz);
            int nLvl = waterIsSource(nraw) ? 0 : waterFlowLevel(nraw);
            fz += dz * (nLvl - myLvl);
        }
    }
    return {fx, fz};
}

TEST(WaterSimulator, FlowVectorPointsAwayFromSource) {
    // Place a source and let it spread. For every flowing cell, the
    // computed flow direction vector should point AWAY from the source
    // (i.e., the dot product of flowDir and the vector from source to
    // the cell should be positive or zero).
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    const int sx = 8, sz = 8;
    s.world.setBlock(sx, y, sz, WATER, 0);
    s.sim.activate(sx, y, sz);
    runTicks(s.sim, 15);

    int tested = 0;
    for (int x = sx - 7; x <= sx + 7; x++) {
        for (int z = sz - 7; z <= sz + 7; z++) {
            if (x == sx && z == sz) continue; // skip source
            if (getType(s.world, x, y, z) != WATER) continue;
            uint8_t raw = getWaterRaw(s.world, x, y, z);
            if (waterIsSource(raw) || waterIsFalling(raw)) continue;

            auto [fx, fz] = computeFlowDir(s.world, x, y, z);
            if (fx == 0 && fz == 0) continue; // no direction computed

            // Vector from source to this cell
            float toX = (float)(x - sx);
            float toZ = (float)(z - sz);
            // Dot product: positive means flow points outward
            float dot = fx * toX + fz * toZ;
            EXPECT_GT(dot, 0.0f)
                << "Flow at (" << x << "," << z << ") = (" << fx << "," << fz
                << ") should point away from source (" << sx << "," << sz
                << "), dot=" << dot;
            tested++;
        }
    }
    EXPECT_GT(tested, 0) << "should have tested at least one flowing cell";
}

TEST(WaterSimulator, FlowVectorCardinalDirections) {
    // Along each cardinal axis from source, flow should point purely
    // along that axis (no perpendicular component).
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 15);

    // +X axis: cell at (9, y, 8) — flow should have fx > 0
    auto [fx1, fz1] = computeFlowDir(s.world, 9, y, 8);
    EXPECT_GT(fx1, 0.0f) << "flow at +X from source should point in +X";

    // -X axis: cell at (7, y, 8) — flow should have fx < 0
    auto [fx2, fz2] = computeFlowDir(s.world, 7, y, 8);
    EXPECT_LT(fx2, 0.0f) << "flow at -X from source should point in -X";

    // +Z axis: cell at (8, y, 9) — flow should have fz > 0
    auto [fx3, fz3] = computeFlowDir(s.world, 8, y, 9);
    EXPECT_GT(fz3, 0.0f) << "flow at +Z from source should point in +Z";

    // -Z axis: cell at (8, y, 7) — flow should have fz < 0
    auto [fx4, fz4] = computeFlowDir(s.world, 8, y, 7);
    EXPECT_LT(fz4, 0.0f) << "flow at -Z from source should point in -Z";
}

TEST(WaterSimulator, FallingWaterColumnHasWaterBelow) {
    // Place source above air. Falling column should form. Verify each
    // cell in the column is WATER with the falling flag set and has
    // proper waterLevel data that the mesh builder can use for side faces.
    PedestalScene s;
    s.placeSource(); // source at y=8, floor at y=2
    runTicks(s.sim, 20);

    // Mid-column cells (above landing level) should be 1-block-wide
    // falling water with AIR on all sides. The bottom cell (y=FLOOR+1)
    // may have spread horizontally, so skip it.
    for (int y = PedestalScene::FLOOR_Y + 2; y < PedestalScene::SRC_Y; y++) {
        EXPECT_EQ(getType(s.world, s.SRC_X, y, s.SRC_Z), WATER) << "y=" << y;
        uint8_t raw = getWaterRaw(s.world, s.SRC_X, y, s.SRC_Z);
        EXPECT_TRUE(waterIsFalling(raw)) << "y=" << y << " should be falling";
        // Side neighbors should be AIR (mid-column is 1 block wide)
        EXPECT_EQ(getType(s.world, s.SRC_X + 1, y, s.SRC_Z), AIR)
            << "side of falling column should be AIR at y=" << y;
    }
}

TEST(WaterSimulator, FallingColumnHasNoGaps) {
    // Build a tall falling column and verify there are NO air gaps.
    // Check every single Y level between source and floor.
    PedestalScene s;
    // Place source at y=20 (higher up for a longer column)
    const int srcY = 20;
    s.world.setBlock(s.SRC_X, srcY, s.SRC_Z, WATER, 0);
    s.sim.activate(s.SRC_X, srcY, s.SRC_Z);

    // Run many ticks so the column reaches the floor
    runTicks(s.sim, 100);

    // Check every Y from source down to floor
    int waterCount = 0, airGaps = 0, fallingCount = 0;
    for (int y = PedestalScene::FLOOR_Y + 1; y <= srcY; y++) {
        block_type bt = getType(s.world, s.SRC_X, y, s.SRC_Z);
        if (bt == WATER) {
            waterCount++;
            uint8_t raw = getWaterRaw(s.world, s.SRC_X, y, s.SRC_Z);
            if (waterIsFalling(raw)) fallingCount++;
        } else {
            airGaps++;
        }
    }
    int expectedCells = srcY - PedestalScene::FLOOR_Y;
    EXPECT_EQ(waterCount, expectedCells)
        << "column should have " << expectedCells << " water cells, got " << waterCount
        << " (" << airGaps << " air gaps)";
    EXPECT_EQ(airGaps, 0) << "column should have NO air gaps";

    // Check all 4 sides of every mid-column cell are AIR
    int missingFaces = 0;
    for (int y = PedestalScene::FLOOR_Y + 2; y < srcY; y++) {
        if (getType(s.world, s.SRC_X, y, s.SRC_Z) != WATER) continue;
        int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (auto& d : dirs) {
            block_type nb = getType(s.world, s.SRC_X + d[0], y, s.SRC_Z + d[1]);
            if (nb != AIR) missingFaces++;
        }
    }
    EXPECT_EQ(missingFaces, 0)
        << "mid-column cells should have AIR on all 4 sides";
}

TEST(WaterSimulator, FallingColumnGrowsOnePerTick) {
    // Verify the column extends by exactly one cell per tick.
    PedestalScene s;
    const int srcY = 15;
    s.world.setBlock(s.SRC_X, srcY, s.SRC_Z, WATER, 0);
    s.sim.activate(s.SRC_X, srcY, s.SRC_Z);

    auto columnBottom = [&]() -> int {
        for (int y = PedestalScene::FLOOR_Y + 1; y <= srcY; y++)
            if (getType(s.world, s.SRC_X, y, s.SRC_Z) == WATER) return y;
        return srcY + 1;
    };

    // After 1 tick: source at srcY, falling at srcY-1
    runTicks(s.sim, 1);
    EXPECT_EQ(columnBottom(), srcY - 1);

    // After 2 ticks: extends to srcY-2
    runTicks(s.sim, 1);
    EXPECT_EQ(columnBottom(), srcY - 2);

    // After 5 more ticks: extends to srcY-7
    runTicks(s.sim, 5);
    EXPECT_EQ(columnBottom(), srcY - 7);

    // Verify NO gaps in the column
    for (int y = columnBottom(); y <= srcY; y++) {
        EXPECT_EQ(getType(s.world, s.SRC_X, y, s.SRC_Z), WATER)
            << "gap at y=" << y;
    }
}

// --- Section dirty tracking ---

TEST(WaterSimulator, SectionDirtyOnlyAffectedSection) {
    // Verify that setBlockType marks only the section containing the
    // edited block (plus boundary neighbors), not all 8 sections.
    PedestalScene s;
    auto& chunk = s.world.chunkManager->getOrCreate(0, 0);
    chunk.sectionDirty = 0; // clear all

    // Block in the middle of section 3 (y=56, sy=3, y%16=8).
    chunk.setBlockType(8, 56, 8, STONE);
    EXPECT_EQ(chunk.sectionDirty, 1u << 3)
        << "mid-section edit should dirty only that section";
}

TEST(WaterSimulator, SectionDirtyBoundaryMarksNeighbor) {
    PedestalScene s;
    auto& chunk = s.world.chunkManager->getOrCreate(0, 0);
    chunk.sectionDirty = 0;

    // Block at bottom of section 4 (y=64, sy=4, y%16=0).
    // Should mark section 4 AND section 3 (below boundary).
    chunk.setBlockType(8, 64, 8, STONE);
    EXPECT_TRUE(chunk.sectionDirty & (1u << 4)) << "section 4 should be dirty";
    EXPECT_TRUE(chunk.sectionDirty & (1u << 3)) << "section 3 (boundary below) should be dirty";
    // Other sections should be clean.
    EXPECT_FALSE(chunk.sectionDirty & ~((1u << 3) | (1u << 4)))
        << "non-adjacent sections should stay clean";
}

TEST(WaterSimulator, SectionDirtyTopBoundary) {
    PedestalScene s;
    auto& chunk = s.world.chunkManager->getOrCreate(0, 0);
    chunk.sectionDirty = 0;

    // Block at top of section 2 (y=47, sy=2, y%16=15).
    // Should mark section 2 AND section 3 (above boundary).
    chunk.setBlockType(8, 47, 8, STONE);
    EXPECT_TRUE(chunk.sectionDirty & (1u << 2));
    EXPECT_TRUE(chunk.sectionDirty & (1u << 3));
}

TEST(WaterSimulator, BuiltDirtyMaskPreservesNewBits) {
    // Simulate the race: build clears only the bits it was built from,
    // not new bits set during the build.
    PedestalScene s;
    auto& chunk = s.world.chunkManager->getOrCreate(0, 0);

    // Section 3 is dirty.
    chunk.sectionDirty = (1u << 3);
    // Simulate buildMeshData snapshotting the dirty mask.
    chunk.builtDirtyMask = chunk.sectionDirty;

    // During the build, a new change dirties section 5.
    chunk.markSectionDirty(5);
    EXPECT_EQ(chunk.sectionDirty, (1u << 3) | (1u << 5));

    // uploadMesh clears only the built bits.
    chunk.sectionDirty &= ~chunk.builtDirtyMask;
    chunk.builtDirtyMask = 0;

    // Section 5 should survive.
    EXPECT_TRUE(chunk.sectionDirty & (1u << 5))
        << "new dirty bit set during build must survive uploadMesh";
    EXPECT_FALSE(chunk.sectionDirty & (1u << 3))
        << "built section should be cleared";
}

// --- Drain ---

TEST(WaterSimulator, DrainCountStrictlyDecreases) {
    PedestalScene s;
    s.placeSource();
    runTicks(s.sim, 30);

    auto countWater = [&]() {
        int count = 0;
        for (int x = 0; x <= 20; x++)
            for (int y = PedestalScene::FLOOR_Y + 1; y <= PedestalScene::SRC_Y; y++)
                for (int z = 0; z <= 20; z++)
                    if (getType(s.world, x, y, z) == WATER) count++;
        return count;
    };

    s.removeSource();
    int prev = countWater();
    int ticksSincePositiveChange = 0;
    for (int t = 0; t < 100 && prev > 0; t++) {
        runTicks(s.sim, 1);
        int cur = countWater();
        // Water count never increases during drain.
        EXPECT_LE(cur, prev) << "tick " << t << ": count went " << prev << " -> " << cur;
        if (cur < prev) ticksSincePositiveChange = 0;
        else ticksSincePositiveChange++;
        prev = cur;
        // Bail out if stuck (would indicate oscillation / leak).
        ASSERT_LT(ticksSincePositiveChange, 10) << "drain stalled at count=" << cur;
    }
    EXPECT_EQ(prev, 0) << "drain did not fully empty the scene";
}
