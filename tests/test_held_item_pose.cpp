#include <gtest/gtest.h>

#include "held_item_pose.h"

namespace {

void expectNear(glm::vec3 a, glm::vec3 b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

glm::vec3 apply(const glm::mat4& m, glm::vec3 p) {
    return glm::vec3(m * glm::vec4(p, 1.0f));
}

} // namespace

TEST(HeldItemPoseTest, RestingSwingIsIdentity) {
    expectNear(heldpose::swingOffset(0.0f), glm::vec3(0.0f));
    glm::mat4 r = heldpose::swingRotation(0.0f);
    expectNear(apply(r, {1, 2, 3}), {1, 2, 3});
}

TEST(HeldItemPoseTest, SwingEndsWhereItStarted) {
    expectNear(heldpose::swingOffset(1.0f), glm::vec3(0.0f));
    expectNear(apply(heldpose::swingRotation(1.0f), {1, 2, 3}), {1, 2, 3});
}

TEST(HeldItemPoseTest, MidSwingFlicksTheItemDownAndInward) {
    // Halfway through, Minecraft's hand has moved toward the screen center
    // (-X) and a blade pointing up (+Y) now points forward/down.
    glm::vec3 off = heldpose::swingOffset(0.5f);
    EXPECT_LT(off.x, -0.3f);
    glm::vec3 up = glm::vec3(heldpose::swingRotation(0.5f) * glm::vec4(0, 1, 0, 0));
    EXPECT_LT(up.y, 0.6f) << "blade tipped well past upright";
    EXPECT_LT(up.z, 0.0f) << "and toward the screen";
}

TEST(HeldItemPoseTest, ArmSpansHandToShoulder) {
    glm::vec3 hand(0.5f, -0.4f, -0.7f), shoulder(0.8f, -0.85f, -0.4f);
    glm::mat4 m = heldpose::armBetween(hand, shoulder, 0.07f);
    expectNear(apply(m, {0, 1, 0}), hand);      // top face center on the hand
    expectNear(apply(m, {0, -1, 0}), shoulder); // bottom face center on the shoulder
    float width = glm::length(apply(m, {1, 0, 0}) - apply(m, {0, 0, 0}));
    EXPECT_NEAR(width, 0.07f, 1e-5f);
}

TEST(HeldItemPoseTest, ArmHandlesStraightUpAndDown) {
    glm::vec3 hand(0, 0, -1);
    expectNear(apply(heldpose::armBetween(hand, hand + glm::vec3(0, -1, 0), 0.1f), {0, -1, 0}),
               hand + glm::vec3(0, -1, 0));
    expectNear(apply(heldpose::armBetween(hand, hand + glm::vec3(0, 1, 0), 0.1f), {0, -1, 0}),
               hand + glm::vec3(0, 1, 0));
}

namespace {

// Normalized device coordinates of a view-space point through the hand projection.
glm::vec2 ndc(glm::vec3 p, float aspect) {
    glm::vec4 c = heldpose::handProjection(aspect) * glm::vec4(p, 1.0f);
    return glm::vec2(c) / c.w;
}

constexpr float WIDE = 16.0f / 9.0f;

} // namespace

TEST(HeldItemPoseTest, ToolRestsDiagonallyInTheLowerRight) {
    glm::mat4 m = heldpose::itemHand(0.0f, WIDE) * heldpose::spriteDisplay(false);
    glm::vec2 handle = ndc(apply(m, heldpose::TOOL_GRIP), WIDE);
    glm::vec2 tip = ndc(apply(m, {14.5f, 14.5f, 0.0f}), WIDE); // sword tip pixel
    EXPECT_GT(handle.x, 0.4f) << "fist on the right";
    EXPECT_LT(handle.y, -0.5f) << "fist near the bottom";
    EXPECT_GT(tip.x, 0.0f) << "tip stays right of the crosshair";
    EXPECT_LT(tip.y, 0.5f) << "and below the top of the screen";
    glm::vec2 blade = tip - handle;
    EXPECT_LT(blade.x, 0.0f) << "blade points left";
    EXPECT_GT(blade.y, 0.0f) << "and up";
    float angle = glm::degrees(std::atan2(blade.y * (1.0f / WIDE), -blade.x)); // on-screen, square pixels
    EXPECT_GT(angle, 30.0f);
    EXPECT_LT(angle, 70.0f);
}

TEST(HeldItemPoseTest, ToolFaceTurnsTowardTheCamera) {
    glm::mat4 m = heldpose::itemHand(0.0f, WIDE) * heldpose::spriteDisplay(false);
    glm::vec3 center = apply(m, {8, 8, 0});
    glm::vec3 normal = glm::normalize(glm::vec3(m * glm::vec4(0, 0, 1, 0)));
    float facing = glm::dot(normal, glm::normalize(-center));
    EXPECT_GT(facing, 0.7f) << "flat face mostly toward the eye";
}

TEST(HeldItemPoseTest, UprightSpritesStandUp) {
    glm::mat4 m = heldpose::itemHand(0.0f, WIDE) * heldpose::spriteDisplay(true);
    glm::vec3 foot = apply(m, heldpose::STEM_GRIP), top = apply(m, {8, 16, 0});
    glm::vec3 up = glm::normalize(top - foot);
    EXPECT_GT(up.y, 0.85f);
    EXPECT_LT(up.x, 0.0f) << "top leans toward the crosshair";
}

TEST(HeldItemPoseTest, HeldBlockShowsTopAndTwoSides) {
    glm::mat4 m = heldpose::itemHand(0.0f, WIDE) * heldpose::blockDisplay();
    glm::vec3 center = apply(m, {0, 0, 0});
    glm::vec3 toEye = glm::normalize(-center);
    int visible = 0;
    for (glm::vec3 n : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
                        glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}) {
        float d = glm::dot(glm::normalize(glm::vec3(m * glm::vec4(n, 0))), toEye);
        if (d > 0.15f) visible++;
    }
    EXPECT_EQ(visible, 3);
    EXPECT_GT(glm::dot(glm::normalize(glm::vec3(m * glm::vec4(0, 1, 0, 0))), toEye), 0.15f) << "top among them";
    // Every corner on screen except at most a sliver below the bottom edge.
    for (int i = 0; i < 8; i++) {
        glm::vec2 p = ndc(apply(m, {i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f}), WIDE);
        EXPECT_GT(p.x, 0.0f);
        EXPECT_LT(p.x, 1.0f);
        EXPECT_GT(p.y, -1.15f);
    }
}

TEST(HeldItemPoseTest, HandKeepsItsScreenSpotAtAnyAspect) {
    for (float aspect : {4.0f / 3.0f, 16.0f / 10.0f, 21.0f / 9.0f}) {
        glm::vec2 ref = ndc(apply(heldpose::itemHand(0.0f, WIDE), {0, 0, 0}), WIDE);
        glm::vec2 p = ndc(apply(heldpose::itemHand(0.0f, aspect), {0, 0, 0}), aspect);
        EXPECT_NEAR(p.x, ref.x, 1e-4f);
        EXPECT_NEAR(p.y, ref.y, 1e-4f);
        glm::vec2 armRef = ndc(apply(heldpose::emptyArm(0.0f, WIDE), {0, 1, 0}), WIDE);
        glm::vec2 arm = ndc(apply(heldpose::emptyArm(0.0f, aspect), {0, 1, 0}), aspect);
        EXPECT_NEAR(arm.x, armRef.x, 1e-4f);
    }
}

TEST(HeldItemPoseTest, EmptyArmReachesInFromTheLowerRight) {
    glm::mat4 m = heldpose::emptyArm(0.0f, WIDE);
    glm::vec2 fist = ndc(apply(m, {0, 1, 0}), WIDE);
    glm::vec2 shoulder = ndc(apply(m, {0, -1, 0}), WIDE);
    EXPECT_GT(fist.x, 0.3f);
    EXPECT_GT(fist.y, -1.0f) << "fist on screen";
    EXPECT_LT(fist.y, 0.0f);
    EXPECT_LT(shoulder.y, -1.0f) << "shoulder below the screen";
    EXPECT_GT(shoulder.x, fist.x) << "arm comes from the right";
}

TEST(HeldItemPoseTest, PunchThrustsTheFistForwardAndBack) {
    glm::vec3 rest = apply(heldpose::emptyArm(0.0f, WIDE), {0, 1, 0});
    glm::vec3 mid = apply(heldpose::emptyArm(0.5f, WIDE), {0, 1, 0});
    expectNear(apply(heldpose::emptyArm(1.0f, WIDE), {0, 1, 0}), rest);
    EXPECT_LT(mid.z, rest.z - 0.2f) << "forward";
    EXPECT_LT(mid.x, rest.x) << "toward the center";
}

TEST(HeldItemPoseTest, SwungItemStaysOnScreen) {
    for (float s : {0.15f, 0.3f, 0.5f, 0.75f}) {
        glm::vec2 hand = ndc(apply(heldpose::itemHand(s, WIDE), {0, 0, 0}), WIDE);
        EXPECT_GT(hand.y, -1.0f) << "swing " << s;
    }
}
