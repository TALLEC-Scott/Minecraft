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
