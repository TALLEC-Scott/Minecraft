#pragma once

// Day/night cycle math. The sun angle is `clock * RADIANS_PER_SECOND` radians, where
// clock is NetSession::syncedTime(); sin(angle) is the sun height, so
// angle 0 = sunrise, π/2 = noon, π = sunset, 3π/2 = midnight.

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace daycycle {

inline constexpr double RADIANS_PER_SECOND = 0.025; // radians per second (~4.2 min per day)
inline constexpr double NOON = glm::half_pi<double>();
inline constexpr double MIDNIGHT = 3.0 * glm::half_pi<double>();

// Seconds to move the clock forward so the sun angle reaches `targetAngle`
// (mod 2π). Always in [0, one full day): /time never runs the sun backwards.
inline double secondsUntil(double clock, double targetAngle) {
    const double twoPi = glm::two_pi<double>();
    double delta = std::fmod(targetAngle - clock * RADIANS_PER_SECOND, twoPi);
    if (delta < 0.0) delta += twoPi;
    return delta / RADIANS_PER_SECOND;
}

} // namespace daycycle
