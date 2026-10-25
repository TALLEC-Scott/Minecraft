#pragma once

// First-person held-item motion, GL-free so tests can pin it.
//
// The attack swing is Minecraft's (ItemInHandRenderer, right hand): the hand
// slides toward the screen center and dips while the item turns sideways and
// flicks down 80°. `swing` is 0..1 over the 0.3 s punch; at 0 the offset is
// zero and the rotation is identity, so the resting pose is untouched.
// The arm is a box stretched from the (moving) hand to a fixed shoulder
// point off-screen, so it stays attached without swinging into view.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace heldpose {

// Hand translation during the swing (view space).
inline glm::vec3 swingOffset(float swing) {
    const float pi = glm::pi<float>();
    float sq = std::sqrt(std::clamp(swing, 0.0f, 1.0f));
    return {-0.4f * std::sin(sq * pi), 0.2f * std::sin(sq * 2.0f * pi), -0.2f * std::sin(swing * pi)};
}

// Item rotation about the hand during the swing.
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

} // namespace heldpose
