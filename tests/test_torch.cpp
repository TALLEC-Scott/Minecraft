// Torches: placement state, support, light, mining, recipe, the 3D stick's
// placement and the pulsing tip (World's pop-off path is exercised in game).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

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

TEST(TorchTest, StandingTorchIsAStickOnTheFloor) {
    glm::ivec3 cell(3, 10, -2);
    glm::vec3 foot(torch::placedModel(cell, torch::STANDING) * glm::vec4(0, 0, 0, 1));
    EXPECT_FLOAT_EQ(foot.x, 3.0f);
    EXPECT_FLOAT_EQ(foot.y, 9.5f) << "foot on the cell's floor";
    EXPECT_FLOAT_EQ(foot.z, -2.0f);
    glm::vec3 tip = torch::tip(cell, torch::STANDING);
    EXPECT_FLOAT_EQ(tip.x, 3.0f);
    EXPECT_FLOAT_EQ(tip.y, 9.5f + torch::STICK_HEIGHT);
}

TEST(TorchTest, WallTorchLeansAwayFromItsWall) {
    for (uint8_t s = 1; s <= 4; ++s) {
        glm::vec3 toWall(torch::SUPPORT[s][0], 0, torch::SUPPORT[s][2]);
        glm::vec3 foot(torch::placedModel({0, 0, 0}, s) * glm::vec4(0, 0, 0, 1));
        EXPECT_NEAR(glm::dot(foot, toWall), 0.5f, 1e-5f) << "foot on the wall plane, state " << int(s);
        EXPECT_NEAR(foot.y, torch::WALL_FOOT_Y - 0.5f, 1e-5f);
        glm::vec3 tip = torch::tip({0, 0, 0}, s);
        float lean = glm::dot(tip - foot, toWall);
        EXPECT_LT(lean, 0.0f) << "tip leans away from the wall, state " << int(s);
        EXPECT_NEAR(lean, -torch::STICK_HEIGHT * std::sin(glm::radians(torch::WALL_TILT_DEGREES)), 1e-5f);
        EXPECT_LT(tip.y, 0.5f) << "tip stays inside the cell";
        EXPECT_NEAR(glm::dot(tip, glm::vec3(toWall.z, 0, -toWall.x)), 0.0f, 1e-5f) << "no sideways drift";
    }
}

TEST(TorchTest, TipPulsesBetweenOrangeAndRed) {
    float minGreen = 10.0f, maxGreen = 0.0f;
    for (int i = 0; i < 100; ++i) {
        glm::vec3 c = torch::tipColor(i * 0.02, 0.0f);
        EXPECT_GT(c.r, c.g) << "always warm";
        minGreen = std::min(minGreen, c.g);
        maxGreen = std::max(maxGreen, c.g);
    }
    EXPECT_GT(maxGreen - minGreen, 0.4f) << "visibly pulses";
    EXPECT_NE(torch::tipPhase({0, 64, 0}), torch::tipPhase({1, 64, 0})) << "neighbours out of step";
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
