#pragma once

// First-person held-item pose and motion, GL-free so tests can pin it.
//
// View space: camera at the origin looking down -Z, +Y up. The pose is
// built like Minecraft's ItemInHandRenderer (right hand): a hand point in
// the lower right of the view, the attack swing about it, then a per-kind
// display transform:
//  * tools: the sprite stands diagonally in the fist, handle at the bottom
//    right, blade / head rising toward the upper left at about 45° on
//    screen, its flat face turned toward the camera and leaning slightly
//    away so it has depth;
//  * upright sprites (flowers, the torch): stand in the fist, top leaning a
//    little toward the crosshair;
//  * blocks: a small cube turned and tipped toward the camera so the top
//    and two sides show, fully in view in the lower right;
//  * the empty hand: the player model's right arm reaching in from the
//    lower-right edge (Minecraft's renderPlayerArm), with its own punch.
// `swing` is 0..1 over the punch; 0 and 1 give the resting pose.
// The hand pass uses its own fixed-FOV projection (handProjection), as
// Minecraft does, so the FOV setting doesn't stretch or move the hand, and
// X offsets follow the aspect ratio so the hand keeps its spot on screen.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace heldpose {

inline constexpr float HAND_FOV_DEGREES = 70.0f;
// The aspect ratio the offsets are tuned for.
inline constexpr float REFERENCE_ASPECT = 16.0f / 9.0f;

inline glm::mat4 handProjection(float aspect) {
    return glm::perspective(glm::radians(HAND_FOV_DEGREES), aspect, 0.05f, 10.0f);
}

inline float aspectScale(float aspect) {
    return aspect / REFERENCE_ASPECT;
}

// Where the fist rests (view space, at REFERENCE_ASPECT).
inline constexpr glm::vec3 HAND_REST{0.62f, -0.45f, -0.75f};

// Hand translation during the swing (view space). Like Minecraft's: a
// quick lift and flick toward the center, then the hand drops down and back
// out to the right, so mid-swing the item sinks partly past the lower-right
// edge of the screen before rising back to rest.
inline glm::vec3 swingOffset(float swing) {
    const float pi = glm::pi<float>();
    swing = std::clamp(swing, 0.0f, 1.0f);
    float sq = std::sqrt(swing);
    float arc = std::sin(swing * pi);
    return {-0.2f * std::sin(sq * pi) + 0.15f * arc, 0.15f * std::sin(sq * 2.0f * pi) - 0.05f * arc, -0.1f * arc};
}

// Item rotation about the hand during the swing: it turns sideways and
// flicks down 80° (Minecraft's applyItemArmAttackTransform).
inline glm::mat4 swingRotation(float swing) {
    const float pi = glm::pi<float>();
    swing = std::clamp(swing, 0.0f, 1.0f);
    float f = std::sin(swing * swing * pi);
    float f1 = std::sin(std::sqrt(swing) * pi);
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(45.0f - 20.0f * f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(-20.0f * f1), glm::vec3(0, 0, 1));
    m = glm::rotate(m, glm::radians(-80.0f * f1), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-45.0f), glm::vec3(0, 1, 0));
    return m;
}

// The fist holding an item: the rest point plus the swing. Items are
// placed relative to this frame by the display transforms below.
inline glm::mat4 itemHand(float swing, float aspect) {
    glm::vec3 rest = HAND_REST * glm::vec3(aspectScale(aspect), 1.0f, 1.0f);
    return glm::translate(glm::mat4(1.0f), rest + swingOffset(swing)) * swingRotation(swing);
}

// Sprite pixel gripped by the fist (sprite pixel coords, x right, y up from
// the bottom-left corner): tools near the end of the handle, which their
// sprites draw at the bottom left; upright sprites at the foot of the stem.
inline constexpr glm::vec3 TOOL_GRIP{2.5f, 2.5f, 0.0f};
inline constexpr glm::vec3 STEM_GRIP{8.0f, 1.0f, 0.0f};

// Sprite items: maps sprite pixel space (16×16 sprite, x right, y up,
// thickness centered on z = 0) into the hand frame.
inline glm::mat4 spriteDisplay(bool upright) {
    // Upright sprites sit lower so the stem runs down into the fist.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, upright ? -0.1f : 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(-40.0f), glm::vec3(0, 1, 0)); // face the camera
    m = glm::rotate(m, glm::radians(-10.0f), glm::vec3(1, 0, 0)); // top leans away
    // Tools: the sprite's handle-to-tip diagonal (45°) turned to 120°, so the
    // blade points up-left with the handle in the fist at the bottom right.
    // Upright sprites just lean their top a little toward the crosshair.
    m = glm::rotate(m, glm::radians(upright ? 15.0f : 75.0f), glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3((upright ? 0.4f : 0.5f) / 16.0f)); // sprite width in view units
    return glm::translate(m, upright ? -STEM_GRIP : -TOOL_GRIP);
}

// Blocks: a unit cube centered on the origin, held a little behind and
// above the fist point, BLOCK_SIZE across, turned 28° and tipped 18° toward the
// camera so its top and both front sides show.
inline constexpr float BLOCK_SIZE = 0.24f;
inline glm::mat4 blockDisplay() {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(-0.06f, 0.11f, -0.1f));
    m = glm::rotate(m, glm::radians(18.0f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(28.0f), glm::vec3(0, 1, 0));
    return glm::scale(m, glm::vec3(BLOCK_SIZE));
}

// Model matrix for a box centered at the origin with half extents
// (halfWidth, 1, halfWidth) — i.e. a unit-half-height column — stretched
// so its top face sits on `hand` and its bottom face on `shoulder`.
inline glm::mat4 armBetween(glm::vec3 hand, glm::vec3 shoulder, float halfWidth) {
    glm::vec3 d = shoulder - hand;
    float len = glm::length(d);
    if (len < 1e-5f) return glm::scale(glm::translate(glm::mat4(1.0f), hand), glm::vec3(halfWidth, 0.0f, halfWidth));
    d /= len;
    // Rotate the box's -Y (hand → shoulder in box space) onto d.
    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    glm::vec3 axis = glm::cross(down, d);
    float axisLen = glm::length(axis);
    glm::mat4 rot(1.0f);
    if (axisLen > 1e-6f)
        rot = glm::rotate(rot, std::atan2(axisLen, glm::dot(down, d)), axis / axisLen);
    else if (glm::dot(down, d) < 0.0f) // pointing straight up: half turn
        rot = glm::rotate(rot, glm::pi<float>(), glm::vec3(1, 0, 0));
    glm::mat4 m = glm::translate(glm::mat4(1.0f), hand) * rot;
    m = glm::translate(m, glm::vec3(0.0f, -len * 0.5f, 0.0f));
    return glm::scale(m, glm::vec3(halfWidth, len * 0.5f, halfWidth));
}

// The empty hand: the right arm reaching in from below the lower-right
// edge, fist toward the screen center. A punch (Minecraft's renderPlayerArm
// motion) thrusts the fist forward and in while the shoulder follows half
// as far, so the arm extends rather than sliding.
inline constexpr glm::vec3 FIST_REST{0.52f, -0.3f, -0.8f};
inline constexpr glm::vec3 SHOULDER_REST{0.74f, -1.1f, -0.55f};
inline constexpr float ARM_HALF_WIDTH = 0.075f;

inline glm::vec3 punchOffset(float swing) {
    const float pi = glm::pi<float>();
    swing = std::clamp(swing, 0.0f, 1.0f);
    float sq = std::sqrt(swing);
    return {-0.3f * std::sin(sq * pi), 0.4f * std::sin(sq * 2.0f * pi) * 0.5f, -0.4f * std::sin(swing * pi)};
}

// Model matrix for the arm as a box centered on the origin with half
// extents (1, 1, 1).
inline glm::mat4 emptyArm(float swing, float aspect) {
    glm::vec3 xs(aspectScale(aspect), 1.0f, 1.0f);
    glm::vec3 punch = punchOffset(swing);
    return armBetween(FIST_REST * xs + punch, SHOULDER_REST * xs + 0.5f * punch, ARM_HALF_WIDTH);
}

} // namespace heldpose
