// Lava through the real WaterSimulator (same stubs as test_water.cpp):
// Minecraft's overworld lava flows 3 blocks, never forms infinite sources,
// and hardens where it meets water.

#include <gtest/gtest.h>
#include "WaterSimulator.h"
#include "world.h"

namespace {

block_type typeAt(World& world, int x, int y, int z) {
    int cx = worldToChunk(x), cz = worldToChunk(z);
    Chunk* c = world.chunkManager->getChunk(cx, cz);
    return c ? c->getBlockType(worldToLocal(x, cx), y, worldToLocal(z, cz)) : AIR;
}

uint8_t rawAt(World& world, int x, int y, int z) {
    int cx = worldToChunk(x), cz = worldToChunk(z);
    Chunk* c = world.chunkManager->getChunk(cx, cz);
    return c ? c->getWaterLevel(worldToLocal(x, cx), y, worldToLocal(z, cz)) : 0;
}

struct LavaScene {
    World world;
    WaterSimulator sim{&world};
    static constexpr int FLOOR = 2;
    static constexpr int Y = FLOOR + 1;

    LavaScene() {
        for (int cx = -1; cx <= 2; cx++)
            for (int cz = -1; cz <= 2; cz++) world.chunkManager->getOrCreate(cx, cz);
        for (int x = -8; x <= 24; x++)
            for (int z = -8; z <= 24; z++) world.setBlock(x, FLOOR, z, STONE, 0);
    }
    void place(block_type t, int x, int y, int z, uint8_t raw = 0) {
        world.setBlock(x, y, z, t, raw);
        sim.activate(x, y, z);
        sim.activateNeighbors(x, y, z);
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; i++) {
            sim.forceNextTick = true;
            sim.tick();
        }
    }
};

} // namespace

TEST(LavaTest, SourceSpreadsThreeBlocks) {
    LavaScene s;
    s.place(LAVA, 8, LavaScene::Y, 8);
    s.run(20);
    for (int d = 1; d <= 3; d++) {
        EXPECT_EQ(typeAt(s.world, 8 + d, LavaScene::Y, 8), LAVA) << d;
        EXPECT_EQ(rawAt(s.world, 8 + d, LavaScene::Y, 8), 2 * d) << d;
    }
    EXPECT_EQ(typeAt(s.world, 12, LavaScene::Y, 8), AIR);
}

TEST(LavaTest, NoInfiniteSources) {
    LavaScene s;
    s.place(LAVA, 8, LavaScene::Y, 8);
    s.place(LAVA, 10, LavaScene::Y, 8);
    s.run(20);
    ASSERT_EQ(typeAt(s.world, 9, LavaScene::Y, 8), LAVA);
    EXPECT_FALSE(waterIsSource(rawAt(s.world, 9, LavaScene::Y, 8))) << "water would refill this gap; lava doesn't";
}

TEST(LavaTest, FlowDrainsWhenTheSourceIsGone) {
    LavaScene s;
    s.place(LAVA, 8, LavaScene::Y, 8);
    s.run(20);
    ASSERT_EQ(typeAt(s.world, 10, LavaScene::Y, 8), LAVA);
    s.world.setBlock(8, LavaScene::Y, 8, AIR, 0);
    s.sim.activateNeighbors(8, LavaScene::Y, 8);
    s.run(20);
    for (int d = 1; d <= 3; d++) EXPECT_EQ(typeAt(s.world, 8 + d, LavaScene::Y, 8), AIR) << d;
}

TEST(LavaTest, SourceNextToWaterBecomesObsidian) {
    LavaScene s;
    // Walls so neither liquid can spread; they only touch each other.
    for (int x = 7; x <= 10; x++)
        for (int z = 7; z <= 9; z++)
            if (!(z == 8 && (x == 8 || x == 9))) s.world.setBlock(x, LavaScene::Y, z, STONE, 0);
    s.place(LAVA, 8, LavaScene::Y, 8);
    s.place(WATER, 9, LavaScene::Y, 8);
    s.run(3);
    EXPECT_EQ(typeAt(s.world, 8, LavaScene::Y, 8), OBSIDIAN);
    EXPECT_EQ(typeAt(s.world, 9, LavaScene::Y, 8), WATER);
}

TEST(LavaTest, FlowingLavaNextToWaterBecomesCobblestone) {
    LavaScene s;
    for (int z = 7; z <= 9; z += 2)
        for (int x = 7; x <= 10; x++) s.world.setBlock(x, LavaScene::Y, z, STONE, 0);
    s.world.setBlock(7, LavaScene::Y, 8, STONE, 0);
    s.world.setBlock(10, LavaScene::Y, 8, STONE, 0);
    s.place(LAVA, 8, LavaScene::Y, 8, 2); // flowing (level 2), no source behind it
    s.place(WATER, 9, LavaScene::Y, 8);
    s.run(1);
    EXPECT_EQ(typeAt(s.world, 8, LavaScene::Y, 8), COBBLESTONE);
}

TEST(LavaTest, LavaPouringOntoWaterMakesStone) {
    LavaScene s;
    s.world.setBlock(8, LavaScene::Y, 8, WATER, 0);
    s.place(LAVA, 8, LavaScene::Y + 1, 8, WATER_FALLING_FLAG);
    s.run(1);
    EXPECT_EQ(typeAt(s.world, 8, LavaScene::Y, 8), STONE);
}

TEST(LavaTest, RulesMatchMinecraft) {
    EXPECT_EQ(LAVA_RULES.levelStep, 2);
    EXPECT_FALSE(LAVA_RULES.infiniteSources);
    EXPECT_DOUBLE_EQ(LAVA_RULES.tickSeconds, 1.5); // 30 game ticks
    EXPECT_TRUE(WATER_RULES.infiniteSources);
}
