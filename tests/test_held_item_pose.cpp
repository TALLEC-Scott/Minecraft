#include <algorithm>
#include <vector>
#include <cmath>

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
    // Early on the hand flicks toward the screen center (-X); halfway
    // through it has dipped a little below rest, and a blade pointing up
    // (+Y) now points forward/down.
    EXPECT_LT(heldpose::swingOffset(0.15f).x, -0.1f);
    EXPECT_LT(heldpose::swingOffset(0.5f).y, 0.0f);
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
    EXPECT_LT(handle.y, -0.8f) << "fist at the bottom edge";
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

namespace {

bool onScreen(glm::vec2 p) {
    return std::abs(p.x) <= 1.0f && std::abs(p.y) <= 1.0f;
}

// Sample points over a held item (view space, after `hand`): the block's
// faces turned toward the eye, a tool's handle-to-tip diagonal, an upright
// sprite's stem.
enum class Held { Block, Tool, Upright };
std::vector<glm::vec3> itemSamples(Held kind, const glm::mat4& hand) {
    std::vector<glm::vec3> pts;
    constexpr int N = 12;
    if (kind == Held::Block) {
        glm::mat4 m = hand * heldpose::blockDisplay();
        glm::vec3 toEye = glm::normalize(-apply(m, {0, 0, 0}));
        for (int axis = 0; axis < 3; axis++)
            for (float side : {-0.5f, 0.5f}) {
                glm::vec3 n(0.0f);
                n[axis] = side;
                if (glm::dot(glm::vec3(m * glm::vec4(n, 0.0f)), toEye) <= 0.0f) continue; // faces we see
                for (int i = 0; i <= N; i++)
                    for (int j = 0; j <= N; j++) {
                        glm::vec3 p(0.0f);
                        p[axis] = side;
                        p[(axis + 1) % 3] = (float)i / N - 0.5f;
                        p[(axis + 2) % 3] = (float)j / N - 0.5f;
                        pts.push_back(apply(m, p));
                    }
            }
    } else {
        glm::mat4 m = hand * heldpose::spriteDisplay(kind == Held::Upright);
        for (int i = 0; i <= 4 * N; i++) {
            float t = 0.5f + 15.0f * (float)i / (4 * N);
            pts.push_back(apply(m, kind == Held::Tool ? glm::vec3(t, t, 0) : glm::vec3(8, t, 0)));
        }
    }
    return pts;
}

struct Visibility {
    float fraction = 0.0f;         // share of the samples on screen
    bool cutRightOrBottom = false; // some sample past the right or bottom edge
    bool topLeftShows = false;     // the topmost and leftmost samples are on screen
};
Visibility visibility(Held kind, float swing) {
    std::vector<glm::vec3> pts = itemSamples(kind, heldpose::itemHand(swing, WIDE));
    Visibility v;
    glm::vec2 top(0.0f, -10.0f), left(10.0f, 0.0f);
    int shown = 0;
    for (glm::vec3 p : pts) {
        glm::vec2 q = ndc(p, WIDE);
        shown += onScreen(q) ? 1 : 0;
        v.cutRightOrBottom |= q.x > 1.0f || q.y < -1.0f;
        if (q.y > top.y) top = q;
        if (q.x < left.x) left = q;
    }
    v.fraction = (float)shown / (float)pts.size();
    v.topLeftShows = onScreen(top) && onScreen(left);
    return v;
}

} // namespace

TEST(HeldItemPoseTest, RestPoseShowsOnlyTheTopLeftOfTheItem) {
    // The fist is off screen past the lower-right corner: every held item is
    // cut off by the right / bottom edge, and only its top-left part shows,
    // as if an unseen hand grips it there.
    for (Held kind : {Held::Block, Held::Tool, Held::Upright}) {
        Visibility v = visibility(kind, 0.0f);
        EXPECT_TRUE(v.cutRightOrBottom) << "kind " << (int)kind;
        EXPECT_TRUE(v.topLeftShows) << "kind " << (int)kind;
        EXPECT_GT(v.fraction, 0.4f) << "kind " << (int)kind;
        EXPECT_LT(v.fraction, 0.75f) << "kind " << (int)kind;
    }
    // The block is cut by both edges.
    glm::mat4 m = heldpose::itemHand(0.0f, WIDE) * heldpose::blockDisplay();
    float maxX = -10.0f, minY = 10.0f;
    for (int i = 0; i < 8; i++) {
        glm::vec2 p = ndc(apply(m, {i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f}), WIDE);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
    }
    EXPECT_GT(maxX, 1.0f);
    EXPECT_LT(minY, -1.0f);
}

TEST(HeldItemPoseTest, SwingKeepsPartOfTheItemInView) {
    // The swing starts and ends at the cropped rest pose and never sinks the
    // item out of view.
    for (Held kind : {Held::Block, Held::Tool, Held::Upright})
        for (float s : {0.15f, 0.3f, 0.5f, 0.75f})
            EXPECT_GT(visibility(kind, s).fraction, 0.15f) << "kind " << (int)kind << " swing " << s;
    expectNear(apply(heldpose::itemHand(1.0f, WIDE), {0, 0, 0}), apply(heldpose::itemHand(0.0f, WIDE), {0, 0, 0}));
}
