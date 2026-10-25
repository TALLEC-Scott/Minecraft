// Torches: placement state, support, light, mining, recipe and the leaned
// wall-torch quads (World's pop-off path is exercised in game).

#include <gtest/gtest.h>

#include "held_light.h"
#include "mining.h"
#include "recipes.h"
#include "torch.h"

TEST(TorchTest, StateFromTheClickedFace) {
    EXPECT_EQ(torch::stateForFace(0, 1, 0), torch::STANDING) << "on top of a block";
    EXPECT_EQ(torch::stateForFace(0, -1, 0), torch::INVALID) << "never on a ceiling";
    // Clicking the -X side of a block puts the torch at -X, hanging on the block at +X.
    uint8_t s = torch::stateForFace(-1, 0, 0);
    ASSERT_TRUE(torch::validState(s));
    EXPECT_EQ(torch::SUPPORT[s][0], 1);
    for (auto [nx, nz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        uint8_t w = torch::stateForFace(nx, 0, nz);
        ASSERT_TRUE(torch::validState(w));
        EXPECT_EQ(torch::SUPPORT[w][0], -nx);
        EXPECT_EQ(torch::SUPPORT[w][2], -nz);
        EXPECT_EQ(torch::SUPPORT[w][1], 0);
    }
}

TEST(TorchTest, OnlyOpaqueSolidsHoldATorch) {
    EXPECT_TRUE(torch::canSupport(STONE));
    EXPECT_TRUE(torch::canSupport(PLANKS));
    EXPECT_FALSE(torch::canSupport(AIR));
    EXPECT_FALSE(torch::canSupport(WATER));
    EXPECT_FALSE(torch::canSupport(LEAVES));
    EXPECT_FALSE(torch::canSupport(TORCH));
    EXPECT_FALSE(torch::canSupport(DANDELION));
}

TEST(TorchTest, BlockProperties) {
    EXPECT_EQ(getBlockLightEmission(TORCH), 14);
    EXPECT_TRUE(hasFlag(TORCH, BF_CROSS));
    EXPECT_FALSE(hasFlag(TORCH, BF_SOLID)) << "walk through it";
    EXPECT_FALSE(isAOOccluder(TORCH));
    EXPECT_EQ(breakTicks(TORCH, ITEM_EMPTY), 0) << "breaks instantly";
    ItemDrop d = blockDrop(TORCH, ITEM_EMPTY, 0.5f);
    EXPECT_EQ(d.id, blockItem(TORCH));
    EXPECT_EQ(d.count, 1);
}

TEST(TorchTest, CoalOverStickMakesFour) {
    constexpr ItemId _ = ITEM_EMPTY;
    ItemId grid[4] = {ITEM_COAL, _, ITEM_STICK, _};
    ItemDrop r = craftResult(grid, 2);
    EXPECT_EQ(r.id, blockItem(TORCH));
    EXPECT_EQ(r.count, 4);
    ItemId upsideDown[4] = {ITEM_STICK, _, ITEM_COAL, _};
    EXPECT_EQ(craftResult(upsideDown, 2).count, 0);
}

TEST(TorchTest, WallTorchLeansIntoItsWall) {
    float a[4][3], b[4][3];
    crossQuadCorners(TORCH, torch::STANDING, 0, 0, 0, a, b);
    EXPECT_FLOAT_EQ(a[0][0], -0.5f) << "a standing torch is an upright cross like a flower";
    uint8_t s = torch::stateForFace(-1, 0, 0); // hangs on the block at +X
    crossQuadCorners(TORCH, s, 0, 0, 0, a, b);
    EXPECT_FLOAT_EQ(a[0][0], 0.0f) << "bottom corners shift half a block toward the wall";
    EXPECT_FLOAT_EQ(a[3][0], 1.0f);
    EXPECT_FLOAT_EQ(a[1][0], -0.5f) << "top corners stay";
    // Flowers ignore the state byte.
    crossQuadCorners(DANDELION, s, 0, 0, 0, a, b);
    EXPECT_FLOAT_EQ(a[0][0], -0.5f);
}

TEST(HeldLightTest, WhatGlowsInHand) {
    EXPECT_EQ(heldItemLight(blockItem(TORCH)), 14);
    EXPECT_EQ(heldItemLight(blockItem(GLOWSTONE)), 15);
    EXPECT_EQ(heldItemLight(blockItem(STONE)), 0);
    EXPECT_EQ(heldItemLight(ITEM_STICK), 0);
    EXPECT_EQ(heldItemLight(ITEM_EMPTY), 0);
}

TEST(HeldLightTest, FallsOffOnePerBlockLikeAPlacedTorch) {
    std::vector<HeldLight> lights{{glm::vec3(0.0f), 14.0f}};
    EXPECT_FLOAT_EQ(heldLightLevelAt(lights, glm::vec3(0.0f)), 14.0f);
    EXPECT_FLOAT_EQ(heldLightLevelAt(lights, glm::vec3(3.0f, 0.0f, 0.0f)), 11.0f);
    EXPECT_FLOAT_EQ(heldLightLevelAt(lights, glm::vec3(0.0f, 20.0f, 0.0f)), 0.0f);
    EXPECT_FLOAT_EQ(heldLightLevelAt({}, glm::vec3(0.0f)), 0.0f) << "empty hands";
    lights.push_back({glm::vec3(10.0f, 0.0f, 0.0f), 15.0f});
    EXPECT_FLOAT_EQ(heldLightLevelAt(lights, glm::vec3(8.0f, 0.0f, 0.0f)), 13.0f) << "the brighter holder wins";
}
