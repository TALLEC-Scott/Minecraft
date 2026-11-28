#pragma once

#include <algorithm>

// Torches (Minecraft's torch / wall_torch): a BF_CROSS block (no collision,
// no sky shadow) that emits light 14, drawn as a 3D stick rather than
// meshed (see TorchModel). A torch stands on the block below it or
// hangs on a wall; which one is kept in the cell's state byte (the byte
// liquids use for their level; non-liquid cells have it free):
//   0 = standing, 1..4 = on the wall at +X, -X, +Z, -Z (the support).
// Torches need an opaque solid block to hang on and pop off when it goes.
// Pure (no GL, no World) so the renderer, world effects and tests share it.

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "cube.h"

namespace torch {

inline constexpr uint8_t STANDING = 0;
inline constexpr uint8_t INVALID = 0xFF;
// Supporting block offset per state.
inline constexpr int SUPPORT[5][3] = {{0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};

inline bool validState(uint8_t s) {
    return s <= 4;
}

// State for a torch placed against the face whose outward normal is
// (nx, ny, nz) (placement cell minus clicked block). Ceilings can't hold
// one (INVALID).
inline uint8_t stateForFace(int nx, int ny, int nz) {
    if (ny > 0) return STANDING;
    if (ny < 0) return INVALID;
    for (uint8_t s = 1; s <= 4; ++s)
        if (SUPPORT[s][0] == -nx && SUPPORT[s][2] == -nz) return s;
    return INVALID;
}

inline bool canSupport(block_type t) {
    return hasFlag(t, BF_SOLID) && hasFlag(t, BF_OPAQUE);
}

// Minecraft's torch model: a 2×2-pixel stick 10 pixels tall (texture
// columns 7-8, rows 6-15, flame on top). Model space: foot centered on
// the origin, stick along +Y.
inline constexpr float STICK_HALF = 1.0f / 16.0f;
inline constexpr float STICK_HEIGHT = 10.0f / 16.0f;
// A wall torch's foot sits on the wall plane, 3.5 pixels up the cell,
// and the stick tilts 22.5° away from the wall (Minecraft's wall_torch).
inline constexpr float WALL_FOOT_Y = 3.5f / 16.0f;
inline constexpr float WALL_TILT_DEGREES = 22.5f;

// Model matrix placing the stick in the cell centered on `cell`.
inline glm::mat4 placedModel(glm::ivec3 cell, uint8_t state) {
    glm::vec3 c(cell);
    if (state == STANDING || !validState(state)) return glm::translate(glm::mat4(1.0f), c - glm::vec3(0, 0.5f, 0));
    glm::vec3 toWall(static_cast<float>(SUPPORT[state][0]), 0.0f, static_cast<float>(SUPPORT[state][2]));
    glm::mat4 m = glm::translate(glm::mat4(1.0f), c + toWall * 0.5f + glm::vec3(0, WALL_FOOT_Y - 0.5f, 0));
    // Rotating about toWall × up by a positive angle tips +Y away from the wall.
    return glm::rotate(m, glm::radians(WALL_TILT_DEGREES), glm::cross(toWall, glm::vec3(0, 1, 0)));
}

// World position of the flame at the top of the stick.
inline glm::vec3 tip(glm::ivec3 cell, uint8_t state) {
    return glm::vec3(placedModel(cell, state) * glm::vec4(0.0f, STICK_HEIGHT, 0.0f, 1.0f));
}

// The glowing cap is the stick's top TIP_HEIGHT (the flame pixels): a
// 2×2×2 cube, 4 pixels on each of its top, left, right, front and back.
inline constexpr float TIP_HEIGHT = 2.0f / 16.0f;

// The cap glows flat (not textured) as a fixed temperature gradient: the
// top face hottest (pale yellow), the sides' upper pixel row bright orange,
// the lower row a deep red-orange. The hues never change; only the
// brightness breathes (about ±12% every 1.2 s, all rings together) with a
// short brighter flare every 3.7 s. A function of time alone, so every
// torch is in sync (and the colours are computed once per frame).
struct TipColors {
    glm::vec3 top, bottom, face;
};
inline const glm::vec3 TIP_FACE_HUE{1.0f, 0.95f, 0.68f};
inline const glm::vec3 TIP_TOP_HUE{1.0f, 0.56f, 0.12f};
inline const glm::vec3 TIP_BOTTOM_HUE{0.55f, 0.17f, 0.035f};
inline constexpr double TIP_PULSE_PERIOD = 1.2;
inline constexpr double TIP_FLARE_PERIOD = 3.7;
inline float tipBrightness(double time) {
    constexpr double TAU = 6.283185307179586;
    float pulse = static_cast<float>(std::sin(time * TAU / TIP_PULSE_PERIOD));
    // pow of a sine: flat most of the period, a sharp short spike at its crest.
    float flare = static_cast<float>(std::pow(std::max(std::sin(time * TAU / TIP_FLARE_PERIOD), 0.0), 16.0));
    return 0.88f + 0.12f * pulse + 0.22f * flare;
}
inline TipColors tipColor(double time) {
    float k = tipBrightness(time);
    return {k * TIP_TOP_HUE, k * TIP_BOTTOM_HUE, k * TIP_FACE_HUE};
}
// Vertex brightness marking the tip's top face for the billboard shader
// (glowMode 3 gives it `tintColorFace`).
inline constexpr float TIP_TOP_FACE_BOOST = 1.2f;

} // namespace torch

// Corners of the two diagonal planes of a cross-quad plant at (wx, wy, wz)
// (block-centered), vertex order bottom, top, top, bottom.
inline void crossQuadCorners(float wx, float wy, float wz, float a[4][3], float b[4][3]) {
    float y0 = wy - 0.5f, y1 = wy + 0.5f;
    const float pa[4][3] = {
        {wx - 0.5f, y0, wz - 0.5f}, {wx - 0.5f, y1, wz - 0.5f}, {wx + 0.5f, y1, wz + 0.5f}, {wx + 0.5f, y0, wz + 0.5f}};
    const float pb[4][3] = {
        {wx - 0.5f, y0, wz + 0.5f}, {wx - 0.5f, y1, wz + 0.5f}, {wx + 0.5f, y1, wz - 0.5f}, {wx + 0.5f, y0, wz - 0.5f}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) {
            a[i][k] = pa[i][k];
            b[i][k] = pb[i][k];
        }
}
