#include <gtest/gtest.h>
#include "collision.h"
#include <set>

// Helper: create a solid checker from a set of block positions
using BlockSet = std::set<std::tuple<int, int, int>>;
auto makeSolidCheck(const BlockSet& solids) {
    return [&solids](int x, int y, int z) { return solids.count({x, y, z}) > 0; };
}

// --- collides() tests ---

TEST(CollisionTest, NoBlocksNeverCollides) {
    BlockSet empty;
    auto check = makeSolidCheck(empty);
    EXPECT_FALSE(collides(glm::vec3(0, 0, 0), check));
    EXPECT_FALSE(collides(glm::vec3(100, 50, 100), check));
}

TEST(CollisionTest, CollidesWithBlockAtFeet) {
    BlockSet solids = {{5, 10, 5}};
    auto check = makeSolidCheck(solids);
    // Player feet at (5.5, 10.0, 5.5) — directly inside the solid block
    EXPECT_TRUE(collides(glm::vec3(5.5f, 10.0f, 5.5f), check));
}

TEST(CollisionTest, CollidesWithBlockAtHead) {
    BlockSet solids = {{5, 11, 5}};
    auto check = makeSolidCheck(solids);
    // Player feet at (5.5, 10.0, 5.5), head at 10.0 + 1.8 = 11.8, overlaps block at y=11
    EXPECT_TRUE(collides(glm::vec3(5.5f, 10.0f, 5.5f), check));
}

TEST(CollisionTest, NoCollisionAboveHead) {
    BlockSet solids = {{5, 13, 5}};
    auto check = makeSolidCheck(solids);
    // Player feet at (5.5, 10.0, 5.5), head at 11.8 — block at y=13 spans [12.5,13.5), above head
    EXPECT_FALSE(collides(glm::vec3(5.5f, 10.0f, 5.5f), check));
}

TEST(CollisionTest, CollidesWhenStraddlingBlocks) {
    // Player straddles X boundary — solid block on one side
    BlockSet solids = {{5, 10, 5}};
    auto check = makeSolidCheck(solids);
    // Player at x=5.2, width extends from 4.9 to 5.5 — overlaps block at x=5
    EXPECT_TRUE(collides(glm::vec3(5.2f, 10.0f, 5.5f), check));
}

// --- resolveVertical() tests ---

TEST(VerticalCollisionTest, FallOntoGround) {
    // Solid floor at y=60
    BlockSet solids;
    for (int x = -2; x <= 2; x++)
        for (int z = -2; z <= 2; z++)
            solids.insert({x, 60, z});
    auto check = makeSolidCheck(solids);

    // Player feet at y=62, falling by -3. Block at y=60 spans [59.5, 60.5), top = 60.5
    auto result = resolveVertical(glm::vec3(0.5f, 62.0f, 0.5f), -3.0f, check);
    EXPECT_TRUE(result.hitGround);
    EXPECT_FLOAT_EQ(result.newFeetY, 60.5f);
}

TEST(VerticalCollisionTest, FallThroughAir) {
    BlockSet empty;
    auto check = makeSolidCheck(empty);

    auto result = resolveVertical(glm::vec3(0.5f, 50.0f, 0.5f), -2.0f, check);
    EXPECT_FALSE(result.hitGround);
    EXPECT_FALSE(result.hitCeiling);
    EXPECT_FLOAT_EQ(result.newFeetY, 48.0f);
}

TEST(VerticalCollisionTest, CeilingStopsJump) {
    // Ceiling block at y=66 spans [65.5, 66.5)
    BlockSet solids = {{0, 66, 0}};
    auto check = makeSolidCheck(solids);

    // Player feet at y=63.5, head at 65.3, jumping up by 0.5 → head would reach 65.8
    // Block bottom at 65.5 stops the player
    auto result = resolveVertical(glm::vec3(0.5f, 63.5f, 0.5f), 0.5f, check);
    EXPECT_TRUE(result.hitCeiling);
    EXPECT_FLOAT_EQ(result.newFeetY, 65.5f - PLAYER_TOTAL_HEIGHT);
}

TEST(VerticalCollisionTest, NoCeilingWhenFarAbove) {
    BlockSet solids = {{0, 70, 0}};
    auto check = makeSolidCheck(solids);

    // Player feet at y=63.0, jumping by 0.5 — ceiling at 70 is far away
    auto result = resolveVertical(glm::vec3(0.5f, 63.0f, 0.5f), 0.5f, check);
    EXPECT_FALSE(result.hitCeiling);
    EXPECT_FLOAT_EQ(result.newFeetY, 63.5f);
}

TEST(VerticalCollisionTest, GroundCheckCoversPlayerWidth) {
    // Block only at x=1, not at x=0 — player straddling the border should still land
    BlockSet solids = {{1, 60, 0}};
    auto check = makeSolidCheck(solids);

    // Player at x=0.8, width from 0.5 to 1.1 — overlaps block at x=1 (spans 0.5 to 1.5)
    auto result = resolveVertical(glm::vec3(0.8f, 62.0f, 0.5f), -3.0f, check);
    EXPECT_TRUE(result.hitGround) << "Player should land on block they partially overlap";
    EXPECT_FLOAT_EQ(result.newFeetY, 60.5f);
}

TEST(VerticalCollisionTest, NoGroundWhenBlockBesideNotUnder) {
    // Block at x=2 — player at x=0.5 doesn't overlap
    BlockSet solids = {{2, 60, 0}};
    auto check = makeSolidCheck(solids);

    auto result = resolveVertical(glm::vec3(0.5f, 62.0f, 0.5f), -3.0f, check);
    EXPECT_FALSE(result.hitGround) << "Player should not land on block they don't overlap";
    EXPECT_FLOAT_EQ(result.newFeetY, 59.0f);
}

TEST(VerticalCollisionTest, ZeroMovementNoCollision) {
    BlockSet solids = {{0, 60, 0}};
    auto check = makeSolidCheck(solids);

    auto result = resolveVertical(glm::vec3(0.5f, 61.0f, 0.5f), 0.0f, check);
    EXPECT_FALSE(result.hitGround);
    EXPECT_FALSE(result.hitCeiling);
    EXPECT_FLOAT_EQ(result.newFeetY, 61.0f);
}

// --- resolveMovement() (horizontal) tests ---

TEST(HorizontalCollisionTest, FreeMoveInAir) {
    BlockSet empty;
    auto check = makeSolidCheck(empty);

    glm::vec3 result = resolveMovement(glm::vec3(5.0f, 10.0f, 5.0f), glm::vec3(1.0f, 0, 1.0f), check);
    EXPECT_FLOAT_EQ(result.x, 6.0f);
    EXPECT_FLOAT_EQ(result.z, 6.0f);
}

TEST(HorizontalCollisionTest, WallBlocksMovement) {
    // Wall at x=6
    BlockSet solids;
    for (int y = 10; y <= 12; y++) solids.insert({6, y, 5});
    auto check = makeSolidCheck(solids);

    // Player at x=5.5 trying to move +1 in X — blocked by wall
    glm::vec3 result = resolveMovement(glm::vec3(5.5f, 10.0f, 5.5f), glm::vec3(1.0f, 0, 0), check);
    EXPECT_FLOAT_EQ(result.x, 5.5f); // didn't move
}

TEST(HorizontalCollisionTest, WallSliding) {
    // Long wall along X at z=8 — player moves diagonally, X slides through, Z blocked
    BlockSet solids;
    for (int y = 10; y <= 12; y++)
        for (int x = -5; x <= 15; x++)
            solids.insert({x, y, 8});
    auto check = makeSolidCheck(solids);

    // Player at (5.0, 10.0, 6.5). Block at z=8 spans [7.5,8.5). Moving (+1, 0, +1)
    // Combined (6.0, 10, 7.5) — player edge at 7.5+0.3=7.8 overlaps [7.5,8.5) → blocked
    // X-only (6.0, 10, 6.5) — no overlap with z=8 wall → slides
    // Z-only (5.0, 10, 7.5) — player edge at 7.8 overlaps wall → blocked
    glm::vec3 result = resolveMovement(glm::vec3(5.0f, 10.0f, 6.5f), glm::vec3(1.0f, 0, 1.0f), check);
    EXPECT_FLOAT_EQ(result.x, 6.0f); // X slid through
    EXPECT_FLOAT_EQ(result.z, 6.5f); // Z blocked by wall
}
