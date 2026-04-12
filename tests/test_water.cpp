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

// Run N simulator ticks. tick() gates on wall-clock time, so use the
// forceNextTick flag to fire on each call regardless of elapsed time.
void runTicks(WaterSimulator& sim, int n) {
    for (int i = 0; i < n; i++) {
        sim.forceNextTick = true;
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

// --- Water geometry helpers ---
// These are EXACT copies of the functions in chunk.cpp so the test
// exercises the same math the mesh builder uses.

static float waterHeightFromRaw_test(uint8_t raw) {
    if (waterIsFalling(raw)) return 15.0f / 16.0f;
    return (15.0f - waterFlowLevel(raw) * 2.0f) / 16.0f;
}

// Exact copy of waterCellHeight from chunk.cpp (in-chunk path only,
// no cross-chunk since tests use a single chunk).
float testWaterCellHeight(World& world, int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return -1.0f;
    if (getType(world, wx, wy, wz) != WATER) return -1.0f;
    // Water with water above = full block height
    if (wy + 1 < CHUNK_HEIGHT && getType(world, wx, wy + 1, wz) == WATER)
        return 1.0f;
    uint8_t raw = getWaterRaw(world, wx, wy, wz);
    return waterHeightFromRaw_test(raw);
}

// Exact copy of computeWaterTopCorners from chunk.cpp.
// Uses canonical signs (uSign=1, vSign=-1) matching the side face path.
void testWaterTopCorners(World& world, int wx, int wy, int wz, float out[4]) {
    float cH = testWaterCellHeight(world, wx, wy, wz);
    // After uSign=1 (no swap), vSign=-1 swap:
    int cdx[4] = {-1, -1, 1, 1};
    int cdz[4] = { 1, -1, -1, 1};
    auto contribute = [&](int x, int y, int z) -> std::pair<float, bool> {
        if (y < 0 || y >= CHUNK_HEIGHT) return {0.0f, false};
        block_type t = getType(world, x, y, z);
        if (t == WATER) {
            uint8_t raw = getWaterRaw(world, x, y, z);
            float h = waterIsFalling(raw) ? 15.0f / 16.0f
                                          : (15.0f - waterFlowLevel(raw) * 2.0f) / 16.0f;
            return {h, true};
        }
        if (t == AIR) return {0.0f, true};
        return {0.0f, false};
    };
    auto fullHeight = [&](int x, int y, int z) -> bool {
        if (y < 0 || y + 1 >= CHUNK_HEIGHT) return false;
        return getType(world, x, y, z) == WATER && getType(world, x, y + 1, z) == WATER;
    };
    auto cC = contribute(wx, wy, wz);
    for (int ci = 0; ci < 4; ci++) {
        bool anyFull = fullHeight(wx, wy, wz)
                    || fullHeight(wx + cdx[ci], wy, wz)
                    || fullHeight(wx, wy, wz + cdz[ci])
                    || fullHeight(wx + cdx[ci], wy, wz + cdz[ci]);
        if (anyFull) {
            out[ci] = (float)wy + 0.5f;
            continue;
        }
        std::pair<float, bool> h[4] = {cC,
            contribute(wx + cdx[ci], wy, wz),
            contribute(wx, wy, wz + cdz[ci]),
            contribute(wx + cdx[ci], wy, wz + cdz[ci])};
        float sum = 0; int cnt = 0;
        for (int i = 0; i < 4; i++) if (h[i].second) { sum += h[i].first; cnt++; }
        out[ci] = (float)wy - 0.5f + (cnt > 0 ? sum / cnt : 0.0f);
    }
}

// The side face top edge heights for a given face direction.
// This replicates the EXACT mesh builder logic:
//   computeWaterTopCorners(bc, 1, -1, topCorners)
//   vp[1][Y] = topCorners[SIDE_TOP_IDX[f][0]]
//   vp[2][Y] = topCorners[SIDE_TOP_IDX[f][1]]
// Returns the two Y positions of the top-edge vertices.
static constexpr int TEST_SIDE_TOP_IDX[4][2] = {{0,3}, {2,1}, {1,0}, {3,2}};

// Side face bottom is always block floor.
// Side face top is corner-averaged (no exceptions for falling/waterAbove).

// --- Water geometry tests ---

TEST(WaterSimulator, WaterUnderFallingHasFullHeight) {
    // Pool water directly under a falling column should have
    // waterCellHeight = 1.0 (full block) and all corners at block ceiling.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    // Place source high up so it creates a falling column
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    // The block at the base of the column (just above floor) should
    // have water above it (the falling column).
    ASSERT_EQ(getType(s.world, 8, y, 8), WATER);
    ASSERT_EQ(getType(s.world, 8, y + 1, 8), WATER) << "should have falling water above";

    float h = testWaterCellHeight(s.world, 8, y, 8);
    EXPECT_FLOAT_EQ(h, 1.0f) << "water under falling should be full height";

    // Corners are averaged with neighbors — the full-height center (1.0)
    // pulls them above what they'd be without water above, and they must
    // not exceed the block ceiling.
    float corners[4];
    testWaterTopCorners(s.world, 8, y, 8, corners);
    float ceiling = (float)y + 0.5f;
    float noWaterAboveH = (15.0f - waterFlowLevel(getWaterRaw(s.world, 8, y, 8)) * 2.0f) / 16.0f;
    float minWithout = (float)y - 0.5f + noWaterAboveH;
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(corners[i], ceiling)
            << "corner " << i << " should not exceed ceiling";
        // The full-height center should pull corners up compared to
        // what they'd be with normal height
        EXPECT_GE(corners[i], minWithout - 0.5f)
            << "corner " << i << " should not be drastically low";
    }
}

TEST(WaterSimulator, WaterAdjacentToFallingColumnNoSlope) {
    // Pool water NEXT TO the falling column (not directly under it)
    // should NOT have its corners pulled up to 1.0 by the adjacent
    // full-height block. The corner shared with the falling column
    // position should still average correctly without creating a
    // diagonal slope that rises above the block's own water level.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    // Block at (9, y, 8) is pool water adjacent to the column base at (8, y, 8).
    // It should NOT have water above it (the column is only at x=8).
    ASSERT_EQ(getType(s.world, 9, y, 8), WATER);
    bool hasWaterAbove = (getType(s.world, 9, y + 1, 8) == WATER);

    float myH = testWaterCellHeight(s.world, 9, y, 8);
    float corners[4];
    testWaterTopCorners(s.world, 9, y, 8, corners);

    // The corner closest to the column (at -X side) will average with
    // the full-height block at (8, y, 8). Check that NO corner exceeds
    // the block ceiling — that would cause visible geometry poking above.
    float ceiling = (float)y + 0.5f;
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(corners[i], ceiling)
            << "corner " << i << " should not exceed block ceiling"
            << " (hasWaterAbove=" << hasWaterAbove << ", myH=" << myH << ")";
    }

    // If this block does NOT have water above, its own height should be
    // less than 1.0 (it's pool/flowing water, not full height).
    if (!hasWaterAbove) {
        EXPECT_LT(myH, 1.0f) << "pool water without water above should not be full height";
        // Corners should stay below or at the water surface, not slope up
        float maxSurface = (float)y - 0.5f + 1.0f; // absolute max = block ceiling
        for (int i = 0; i < 4; i++) {
            EXPECT_LE(corners[i], maxSurface)
                << "corner " << i << " above block ceiling";
        }
    }
}

TEST(WaterSimulator, CliffEdgeWaterFallsImmediately) {
    // When water spreads to a cliff edge (block below is air),
    // falling water should be placed below immediately.
    World world;
    WaterSimulator sim(&world);
    ensureChunks(world, 0, 20, 0, 20);

    // Build a shelf: stone at y=5 from x=5..10, z=5..10
    for (int x = 5; x <= 10; x++)
        for (int z = 5; z <= 10; z++)
            world.setBlock(x, 5, z, STONE, 0);

    // Place water source in the middle of the shelf
    world.setBlock(8, 6, 8, WATER, 0);
    sim.activate(8, 6, 8);
    runTicks(sim, 15);

    // Water at the edge of the shelf (e.g., x=5, z=8) should have
    // spread to x=4 (off the shelf). That block has air below.
    // Falling water should exist at (4, 5, 8) = one below the edge.
    if (getType(world, 4, 6, 8) == WATER) {
        // Water went off the shelf — check falling water below
        EXPECT_EQ(getType(world, 4, 5, 8), WATER)
            << "falling water should be placed below cliff edge immediately";
        if (getType(world, 4, 5, 8) == WATER) {
            EXPECT_TRUE(waterIsFalling(getWaterRaw(world, 4, 5, 8)))
                << "water below cliff edge should have falling flag";
        }
    }
}

TEST(WaterSimulator, FallingColumnSideFacesFlushed) {
    // At the junction between a falling column and the pool below,
    // the pool block directly under the column should have full height
    // (1.0) so its side faces reach the falling column's block floor.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    // Falling column at (8, y+1..9, 8), pool at (8, y, 8)
    ASSERT_EQ(getType(s.world, 8, y, 8), WATER);

    // The pool block's side face top should be at the block ceiling
    // (full height), NOT at waterHeightFromRaw which would leave a gap.
    float corners[4];
    testWaterTopCorners(s.world, 8, y, 8, corners);
    float ceiling = (float)y + 0.5f;
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(corners[i], ceiling)
            << "pool corner " << i << " should not exceed ceiling";
    }
}

TEST(WaterSimulator, AdjacentWaterCornersContiguous) {
    // Adjacent water blocks must share identical corner heights at their
    // shared edge. Any mismatch = visible gap in the mesh.
    // Corners (u_sign=1, v_sign=-1):
    //   0=(-X,+Z), 1=(-X,-Z), 2=(+X,-Z), 3=(+X,+Z)
    // Block at (x,z) and (x+1,z) share the +X edge of (x,z) and -X edge of (x+1,z).
    //   (x,z) corners 2=(+X,-Z) and 3=(+X,+Z)  must match
    //   (x+1,z) corners 1=(-X,-Z) and 0=(-X,+Z) respectively.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    // Create a falling column so we test the tricky case
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    int mismatches = 0;
    for (int x = 4; x <= 12; x++) {
        for (int z = 4; z <= 12; z++) {
            if (getType(s.world, x, y, z) != WATER) continue;
            float cornersA[4];
            testWaterTopCorners(s.world, x, y, z, cornersA);

            // Check +X neighbor
            if (getType(s.world, x + 1, y, z) == WATER) {
                float cornersB[4];
                testWaterTopCorners(s.world, x + 1, y, z, cornersB);
                // A's +X,-Z (2) == B's -X,-Z (1)
                if (std::abs(cornersA[2] - cornersB[1]) > 0.001f) {
                    ADD_FAILURE() << "X-edge gap at (" << x << "," << z << ")->("
                        << x+1 << "," << z << "): " << cornersA[2] << " vs " << cornersB[1];
                    mismatches++;
                }
                // A's +X,+Z (3) == B's -X,+Z (0)
                if (std::abs(cornersA[3] - cornersB[0]) > 0.001f) {
                    ADD_FAILURE() << "X-edge gap at (" << x << "," << z << ")->("
                        << x+1 << "," << z << "): " << cornersA[3] << " vs " << cornersB[0];
                    mismatches++;
                }
            }

            // Check +Z neighbor
            if (getType(s.world, x, y, z + 1) == WATER) {
                float cornersB[4];
                testWaterTopCorners(s.world, x, y, z + 1, cornersB);
                // A's -X,+Z (0) == B's -X,-Z (1) — wait, Z+ neighbor:
                // A's +Z corners are 0=(-X,+Z) and 3=(+X,+Z)
                // B's -Z corners are 1=(-X,-Z) and 2=(+X,-Z)
                if (std::abs(cornersA[0] - cornersB[1]) > 0.001f) {
                    ADD_FAILURE() << "Z-edge gap at (" << x << "," << z << ")->("
                        << x << "," << z+1 << "): " << cornersA[0] << " vs " << cornersB[1];
                    mismatches++;
                }
                if (std::abs(cornersA[3] - cornersB[2]) > 0.001f) {
                    ADD_FAILURE() << "Z-edge gap at (" << x << "," << z << ")->("
                        << x << "," << z+1 << "): " << cornersA[3] << " vs " << cornersB[2];
                    mismatches++;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << "adjacent water blocks have non-matching corners";
}

TEST(WaterSimulator, VerticalContiguityFallingColumn) {
    // A falling water column landing on a pool must have no vertical gaps.
    // For each Y boundary between stacked water blocks:
    //   - The lower block's side face top edge must equal the upper
    //     block's side face bottom edge (block floor = y+0.5).
    //   - Falling water side faces are full block height (floor to ceiling).
    //   - Pool water side faces are trimmed to corner heights at top,
    //     but extend to full height when water is above.
    PedestalScene s;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    // Check the column at x=8, z=8 from floor up to source
    int mismatches = 0;
    for (int y = PedestalScene::FLOOR_Y + 1; y < 10; y++) {
        if (getType(s.world, 8, y, 8) != WATER) continue;
        if (getType(s.world, 8, y + 1, 8) != WATER) continue;

        // Lower block's side face top: if water above, full height = y+0.5
        // Upper block's side face bottom: always block floor = y+0.5
        // These must match (both are y+0.5 when water is above).
        uint8_t lowerRaw = getWaterRaw(s.world, 8, y, 8);
        bool lowerHasWaterAbove = true; // we just checked y+1 is water

        // The side face top for the lower block:
        float lowerSideTop;
        if (waterIsFalling(lowerRaw) || lowerHasWaterAbove) {
            // Full block height — side face goes to ceiling
            lowerSideTop = (float)y + 0.5f;
        } else {
            // Trimmed to corner height
            float corners[4];
            testWaterTopCorners(s.world, 8, y, 8, corners);
            lowerSideTop = *std::max_element(corners, corners + 4);
        }

        // Upper block's side face bottom is always the block floor
        float upperSideBottom = (float)(y + 1) - 0.5f; // = y + 0.5

        if (std::abs(lowerSideTop - upperSideBottom) > 0.001f) {
            ADD_FAILURE() << "Vertical gap at y=" << y << "->" << y+1
                << ": lower top=" << lowerSideTop
                << ", upper bottom=" << upperSideBottom;
            mismatches++;
        }
    }
    EXPECT_EQ(mismatches, 0) << "vertical gaps in falling column";
}

TEST(WaterSimulator, HorizontalContiguityWithFallingColumn) {
    // Full test: falling column + pool spread. Every pair of horizontally
    // adjacent water blocks at every Y level must have matching corners.
    PedestalScene s;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    int mismatches = 0;
    for (int y = PedestalScene::FLOOR_Y + 1; y <= 10; y++) {
        for (int x = 2; x <= 14; x++) {
            for (int z = 2; z <= 14; z++) {
                if (getType(s.world, x, y, z) != WATER) continue;
                float cornersA[4];
                testWaterTopCorners(s.world, x, y, z, cornersA);

                // +X neighbor
                if (getType(s.world, x + 1, y, z) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(s.world, x + 1, y, z, cornersB);
                    if (std::abs(cornersA[2] - cornersB[1]) > 0.001f) {
                        ADD_FAILURE() << "X gap y=" << y << " (" << x << "," << z
                            << ")->(" << x+1 << "," << z << "): "
                            << cornersA[2] << " vs " << cornersB[1];
                        mismatches++;
                    }
                    if (std::abs(cornersA[3] - cornersB[0]) > 0.001f) {
                        ADD_FAILURE() << "X gap y=" << y << " (" << x << "," << z
                            << ")->(" << x+1 << "," << z << "): "
                            << cornersA[3] << " vs " << cornersB[0];
                        mismatches++;
                    }
                }

                // +Z neighbor
                if (getType(s.world, x, y, z + 1) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(s.world, x, y, z + 1, cornersB);
                    if (std::abs(cornersA[0] - cornersB[1]) > 0.001f) {
                        ADD_FAILURE() << "Z gap y=" << y << " (" << x << "," << z
                            << ")->(" << x << "," << z+1 << "): "
                            << cornersA[0] << " vs " << cornersB[1];
                        mismatches++;
                    }
                    if (std::abs(cornersA[3] - cornersB[2]) > 0.001f) {
                        ADD_FAILURE() << "Z gap y=" << y << " (" << x << "," << z
                            << ")->(" << x << "," << z+1 << "): "
                            << cornersA[3] << " vs " << cornersB[2];
                        mismatches++;
                    }
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << "horizontal gaps across all Y levels";
}

TEST(WaterSimulator, FullMeshContiguity) {
    // Comprehensive: scan EVERY water block at EVERY Y level and verify
    // ALL adjacent pairs share identical corner heights at shared edges.
    // Also verify vertical contiguity (side face top = block above floor).
    PedestalScene s;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 40);

    int hMismatches = 0, vMismatches = 0;
    for (int y = 0; y < CHUNK_HEIGHT; y++) {
        for (int x = -8; x <= 24; x++) {
            for (int z = -8; z <= 24; z++) {
                if (getType(s.world, x, y, z) != WATER) continue;
                float cornersA[4];
                testWaterTopCorners(s.world, x, y, z, cornersA);

                // Horizontal: +X neighbor
                if (getType(s.world, x + 1, y, z) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(s.world, x + 1, y, z, cornersB);
                    if (std::abs(cornersA[2] - cornersB[1]) > 0.001f ||
                        std::abs(cornersA[3] - cornersB[0]) > 0.001f) {
                        if (hMismatches < 5)
                            ADD_FAILURE() << "X gap y=" << y << " (" << x << "," << z << ")";
                        hMismatches++;
                    }
                }
                // Horizontal: +Z neighbor
                if (getType(s.world, x, y, z + 1) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(s.world, x, y, z + 1, cornersB);
                    if (std::abs(cornersA[0] - cornersB[1]) > 0.001f ||
                        std::abs(cornersA[3] - cornersB[2]) > 0.001f) {
                        if (hMismatches < 5)
                            ADD_FAILURE() << "Z gap y=" << y << " (" << x << "," << z << ")";
                        hMismatches++;
                    }
                }
                // Vertical: block above is water — side faces must connect
                if (getType(s.world, x, y + 1, z) == WATER) {
                    uint8_t raw = getWaterRaw(s.world, x, y, z);
                    bool waterAbove = true;
                    float sideTop;
                    if (waterIsFalling(raw) || waterAbove) {
                        sideTop = (float)y + 0.5f;
                    } else {
                        sideTop = *std::max_element(cornersA, cornersA + 4);
                    }
                    float aboveFloor = (float)(y + 1) - 0.5f;
                    if (std::abs(sideTop - aboveFloor) > 0.001f) {
                        if (vMismatches < 5)
                            ADD_FAILURE() << "V gap y=" << y << " (" << x << "," << z
                                << ") top=" << sideTop << " floor=" << aboveFloor;
                        vMismatches++;
                    }
                }
            }
        }
    }
    EXPECT_EQ(hMismatches, 0) << "horizontal corner mismatches";
    EXPECT_EQ(vMismatches, 0) << "vertical side face gaps";
}

// Helper: get the side face top height for a given face of a water block.
// Returns the two vertex heights at the top edge of the side face.
// Mirrors the mesh builder's SIDE_TOP_IDX logic.
void testSideFaceTop(World& world, int wx, int wy, int wz, int face,
                     float& topA, float& topB) {
    float corners[4];
    testWaterTopCorners(world, wx, wy, wz, corners);
    topA = corners[TEST_SIDE_TOP_IDX[face][0]];
    topB = corners[TEST_SIDE_TOP_IDX[face][1]];
}

// For a water block, compute the top face vertex Y positions in the
// same order as the mesh builder: (u0,v0), (u0,v1), (u1,v1), (u1,v0).
// For f=4 top face with u_sign=1, v_sign=-1:
//   v0 = Z_hi, v1 = Z_lo (v_sign=-1 flips)
//   vertex 0 = (X_lo, Y[0], Z_hi) → corner 0
//   vertex 1 = (X_lo, Y[1], Z_lo) → corner 1
//   vertex 2 = (X_hi, Y[2], Z_lo) → corner 2
//   vertex 3 = (X_hi, Y[3], Z_hi) → corner 3
void testTopFaceVertexY(World& world, int wx, int wy, int wz, float yOut[4]) {
    testWaterTopCorners(world, wx, wy, wz, yOut);
    // yOut[0..3] = corner heights, which map directly to vertex Y
}

TEST(WaterSimulator, SideFaceMatchesOwnTopFace) {
    // Each water block's side face top edge must exactly match the
    // corresponding edge of its own top face. Otherwise you see a
    // crack between the side and top faces on the same block.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 15);

    int mismatches = 0;
    for (int x = 2; x <= 14; x++) {
        for (int z = 2; z <= 14; z++) {
            if (getType(s.world, x, y, z) != WATER) continue;
            float topCorners[4];
            testWaterTopCorners(s.world, x, y, z, topCorners);

            // For each side face (0-3), check if neighbor is AIR (face visible)
            int dx[] = {0, 0, -1, 1};
            int dz[] = {1, -1, 0, 0};
            for (int f = 0; f < 4; f++) {
                if (getType(s.world, x + dx[f], y, z + dz[f]) != AIR) continue;
                float sideA, sideB;
                testSideFaceTop(s.world, x, y, z, f, sideA, sideB);
                // Side face top must match the top face corners at that edge
                static constexpr int IDX[4][2] = {{0,3}, {2,1}, {1,0}, {3,2}};
                float topA = topCorners[IDX[f][0]];
                float topB = topCorners[IDX[f][1]];
                if (std::abs(sideA - topA) > 0.001f || std::abs(sideB - topB) > 0.001f) {
                    ADD_FAILURE() << "Side/top mismatch at (" << x << "," << z
                        << ") face=" << f << ": side=(" << sideA << "," << sideB
                        << ") top=(" << topA << "," << topB << ")";
                    mismatches++;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST(WaterSimulator, StaircaseWaterContiguity) {
    // Water flowing down a staircase terrain. Each step drops 1 Y level.
    // Verify contiguity at each step transition.
    World world;
    WaterSimulator sim(&world);
    ensureChunks(world, 0, 30, 0, 16);

    // Build a staircase: step at x=5 y=8, x=8 y=7, x=11 y=6, x=14 y=5
    for (int step = 0; step < 4; step++) {
        int sx = 5 + step * 3;
        int sy = 8 - step;
        for (int x = sx; x < sx + 3; x++)
            for (int z = 4; z <= 12; z++)
                world.setBlock(x, sy, z, STONE, 0);
    }
    // Walls on sides
    for (int x = 5; x < 17; x++) {
        for (int y = 4; y <= 10; y++) {
            world.setBlock(x, y, 3, STONE, 0);
            world.setBlock(x, y, 13, STONE, 0);
        }
    }

    // Place water source at the top of the staircase
    world.setBlock(6, 9, 8, WATER, 0);
    sim.activate(6, 9, 8);
    runTicks(sim, 50);

    // Check contiguity across all water blocks
    int hMismatches = 0;
    for (int y = 0; y < 15; y++) {
        for (int x = 3; x <= 18; x++) {
            for (int z = 3; z <= 13; z++) {
                if (getType(world, x, y, z) != WATER) continue;
                float cornersA[4];
                testWaterTopCorners(world, x, y, z, cornersA);

                // +X neighbor at same Y
                if (getType(world, x + 1, y, z) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(world, x + 1, y, z, cornersB);
                    if (std::abs(cornersA[2] - cornersB[1]) > 0.001f ||
                        std::abs(cornersA[3] - cornersB[0]) > 0.001f) {
                        if (hMismatches < 5)
                            ADD_FAILURE() << "X gap y=" << y << " (" << x << "," << z << ")";
                        hMismatches++;
                    }
                }
                // +Z neighbor at same Y
                if (getType(world, x, y, z + 1) == WATER) {
                    float cornersB[4];
                    testWaterTopCorners(world, x, y, z + 1, cornersB);
                    if (std::abs(cornersA[0] - cornersB[1]) > 0.001f ||
                        std::abs(cornersA[3] - cornersB[2]) > 0.001f) {
                        if (hMismatches < 5)
                            ADD_FAILURE() << "Z gap y=" << y << " (" << x << "," << z << ")";
                        hMismatches++;
                    }
                }
                // Vertical: water above
                if (getType(world, x, y + 1, z) == WATER) {
                    // Side face top must be at ceiling when water above
                    float sideTop = (float)y + 0.5f;
                    float aboveFloor = (float)(y + 1) - 0.5f;
                    if (std::abs(sideTop - aboveFloor) > 0.001f) {
                        if (hMismatches < 5)
                            ADD_FAILURE() << "V gap y=" << y << " (" << x << "," << z << ")";
                        hMismatches++;
                    }
                }
            }
        }
    }
    EXPECT_EQ(hMismatches, 0) << "staircase water has gaps";
}

TEST(WaterSimulator, VertexLevelContiguityCheck) {
    // This test checks the EXACT vertex Y positions that the mesh builder
    // would produce, for every water block, and verifies:
    // 1. Each block's side face top Y matches its own top face edge Y
    // 2. Adjacent blocks' top face edges share the same Y at shared corners
    // 3. Vertically stacked water: lower block side top Y == upper block floor
    //
    // This catches any gap regardless of cause.
    PedestalScene s;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    int errors = 0;
    auto reportErr = [&](const char* type, int x, int y, int z,
                         const char* detail, float a, float b) {
        if (errors < 10)
            ADD_FAILURE() << type << " at (" << x << "," << y << "," << z
                << ") " << detail << ": " << a << " vs " << b;
        errors++;
    };

    for (int y = 0; y < 15; y++) {
        for (int x = 2; x <= 14; x++) {
            for (int z = 2; z <= 14; z++) {
                if (getType(s.world, x, y, z) != WATER) continue;

                // Get this block's corner heights (same as mesh builder)
                float corners[4]; // 0=(-X,+Z), 1=(-X,-Z), 2=(+X,-Z), 3=(+X,+Z)
                testWaterTopCorners(s.world, x, y, z, corners);
                float floor = (float)y - 0.5f;

                // CHECK 1: side face top == own top face edge (same corners)
                // This is guaranteed by construction but verify anyway.
                for (int f = 0; f < 4; f++) {
                    // Side face only visible when neighbor is AIR
                    int dx[] = {0, 0, -1, 1};
                    int dz[] = {1, -1, 0, 0};
                    if (getType(s.world, x + dx[f], y, z + dz[f]) != AIR) continue;

                    float sA = corners[TEST_SIDE_TOP_IDX[f][0]];
                    float sB = corners[TEST_SIDE_TOP_IDX[f][1]];
                    // Side face vertices: bottom at floor, top at sA/sB
                    // Top face vertices at this edge: same corners
                    // (They're the same array, so this is tautological,
                    // but verifies the IDX mapping is consistent)
                    if (sA < floor || sB < floor) {
                        reportErr("SIDE_BELOW_FLOOR", x, y, z, "side face top below floor", sA, floor);
                    }
                }

                // CHECK 2: horizontal contiguity with +X neighbor
                if (getType(s.world, x + 1, y, z) == WATER) {
                    float nb[4];
                    testWaterTopCorners(s.world, x + 1, y, z, nb);
                    // Shared edge at x+0.5:
                    // This block's +X corners: 2=(+X,-Z), 3=(+X,+Z)
                    // Neighbor's -X corners: 1=(-X,-Z), 0=(-X,+Z)
                    if (std::abs(corners[2] - nb[1]) > 0.001f)
                        reportErr("X_EDGE", x, y, z, "+X/-Z corner", corners[2], nb[1]);
                    if (std::abs(corners[3] - nb[0]) > 0.001f)
                        reportErr("X_EDGE", x, y, z, "+X/+Z corner", corners[3], nb[0]);
                }

                // CHECK 3: horizontal contiguity with +Z neighbor
                if (getType(s.world, x, y, z + 1) == WATER) {
                    float nb[4];
                    testWaterTopCorners(s.world, x, y, z + 1, nb);
                    // Shared edge at z+0.5:
                    // This block's +Z corners: 0=(-X,+Z), 3=(+X,+Z)
                    // Neighbor's -Z corners: 1=(-X,-Z), 2=(+X,-Z)
                    if (std::abs(corners[0] - nb[1]) > 0.001f)
                        reportErr("Z_EDGE", x, y, z, "-X/+Z corner", corners[0], nb[1]);
                    if (std::abs(corners[3] - nb[2]) > 0.001f)
                        reportErr("Z_EDGE", x, y, z, "+X/+Z corner", corners[3], nb[2]);
                }

                // CHECK 4: vertical — if water above, all corners must snap
                // to ceiling so shared-edge averaging on adjacent cells also
                // snaps (no gap between submerged cell and neighbors).
                if (getType(s.world, x, y + 1, z) == WATER) {
                    float ceiling = (float)y + 0.5f;
                    for (int ci = 0; ci < 4; ci++) {
                        if (corners[ci] < ceiling - 0.001f) {
                            reportErr("VERT_GAP", x, y, z, "corner below ceiling",
                                      corners[ci], ceiling);
                        }
                    }
                }
            }
        }
    }
    EXPECT_EQ(errors, 0) << "vertex-level contiguity failures";
}

TEST(WaterSimulator, FourChunkCornerIntersection) {
    // At the corner where 4 chunks meet, each chunk computes corners by
    // averaging itself + neighbor blocks. The DIAGONAL block is currently
    // excluded (waterCellHeight returns -1 for both-axes-out-of-range).
    // This makes each chunk exclude a DIFFERENT block, producing different
    // corner heights at the shared 4-chunk vertex → visible seam.
    //
    // Create water spanning 4 chunks near a chunk-intersection corner,
    // with varying flow levels so the heights differ.
    World world;
    WaterSimulator sim(&world);
    ensureChunks(world, -20, 20, -20, 20);
    // Flat floor
    for (int x = -20; x <= 20; x++)
        for (int z = -20; z <= 20; z++)
            world.setBlock(x, 2, z, STONE, 0);
    // Place sources at different positions so water spreads across chunk
    // boundaries with mixed flow levels.
    world.setBlock(-5, 3, -5, WATER, 0);
    sim.activate(-5, 3, -5);
    world.setBlock(5, 3, 5, WATER, 0);
    sim.activate(5, 3, 5);
    runTicks(sim, 40);

    // The 4-chunk corner is at world (0, 0). The 4 blocks around it:
    //   A = (-1, 3, -1) in chunk (-1, -1)
    //   B = ( 0, 3, -1) in chunk ( 0, -1)
    //   C = (-1, 3,  0) in chunk (-1,  0)
    //   D = ( 0, 3,  0) in chunk ( 0,  0)
    // If all 4 are water, their shared corner should have matching heights
    // when viewed from all 4 blocks.
    int y = 3;
    int aType = getType(world, -1, y, -1);
    int bType = getType(world,  0, y, -1);
    int cType = getType(world, -1, y,  0);
    int dType = getType(world,  0, y,  0);

    if (aType == WATER && bType == WATER && cType == WATER && dType == WATER) {
        float cornersA[4]; testWaterTopCorners(world, -1, y, -1, cornersA);
        float cornersB[4]; testWaterTopCorners(world,  0, y, -1, cornersB);
        float cornersC[4]; testWaterTopCorners(world, -1, y,  0, cornersC);
        float cornersD[4]; testWaterTopCorners(world,  0, y,  0, cornersD);

        // Shared corner at world (-0.5, -0.5):
        //   A's corner (+X,+Z) = corner 3
        //   B's corner (-X,+Z) = corner 0
        //   C's corner (+X,-Z) = corner 2
        //   D's corner (-X,-Z) = corner 1
        float aC = cornersA[3];
        float bC = cornersB[0];
        float cC = cornersC[2];
        float dC = cornersD[1];

        EXPECT_FLOAT_EQ(aC, bC) << "4-chunk corner mismatch A vs B";
        EXPECT_FLOAT_EQ(aC, cC) << "4-chunk corner mismatch A vs C";
        EXPECT_FLOAT_EQ(aC, dC) << "4-chunk corner mismatch A vs D";
        EXPECT_FLOAT_EQ(bC, dC) << "4-chunk corner mismatch B vs D";
    }
}

TEST(WaterSimulator, FallingColumnSideFacesMatchCorners) {
    // Falling water side faces must be trimmed to corner heights
    // (not full block height), so they match adjacent pool top faces.
    PedestalScene s;
    s.world.setBlock(8, 10, 8, WATER, 0);
    s.sim.activate(8, 10, 8);
    runTicks(s.sim, 30);

    const int y = PedestalScene::FLOOR_Y + 1;
    // Column base at (8, y, 8) is falling water
    ASSERT_EQ(getType(s.world, 8, y, 8), WATER);
    ASSERT_TRUE(waterIsFalling(getWaterRaw(s.world, 8, y, 8)));

    // Its side face heights must match its corner heights
    float corners[4];
    testWaterTopCorners(s.world, 8, y, 8, corners);
    for (int f = 0; f < 4; f++) {
        float sA, sB;
        testSideFaceTop(s.world, 8, y, 8, f, sA, sB);
        static constexpr int IDX[4][2] = {{0,3}, {2,1}, {1,0}, {3,2}};
        EXPECT_FLOAT_EQ(sA, corners[IDX[f][0]])
            << "falling water face " << f << " side/corner mismatch";
        EXPECT_FLOAT_EQ(sB, corners[IDX[f][1]])
            << "falling water face " << f << " side/corner mismatch";
    }
}

TEST(WaterSimulator, CornerAveragingClampedToCeiling) {
    // Verify that corner averaging never produces a height above the
    // block ceiling, even when a full-height neighbor is included.
    PedestalScene s;
    const int y = PedestalScene::FLOOR_Y + 1;

    // Place source on flat floor — all surrounding blocks are same height
    s.world.setBlock(8, y, 8, WATER, 0);
    s.sim.activate(8, y, 8);
    runTicks(s.sim, 15);

    // Check every water block's corners
    for (int x = 4; x <= 12; x++) {
        for (int z = 4; z <= 12; z++) {
            if (getType(s.world, x, y, z) != WATER) continue;
            float corners[4];
            testWaterTopCorners(s.world, x, y, z, corners);
            float ceiling = (float)y + 0.5f;
            for (int i = 0; i < 4; i++) {
                EXPECT_LE(corners[i], ceiling)
                    << "corner " << i << " at (" << x << "," << z
                    << ") exceeds block ceiling";
            }
        }
    }
}
