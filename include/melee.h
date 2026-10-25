#pragma once

// Minecraft Java attack-cooldown math (Player.attack / getAttackStrengthScale).
// Swinging before the cooldown refills still hits, for reduced damage:
//   strength = clamp((sinceLastSwing + half a tick) / cooldown, 0, 1)
//   damage   = base * (0.2 + strength² * 0.8)
// Critical hits (×1.5, falling and not on the ground) need strength > 0.9.
// GL-free so tests can pin the numbers.

#include <algorithm>

namespace melee {

inline constexpr double HALF_TICK = 0.025; // Minecraft adds 0.5 ticks

inline float attackStrength(double secondsSinceLastSwing, double cooldownSeconds) {
    if (cooldownSeconds <= 0.0) return 1.0f;
    return static_cast<float>(std::clamp((secondsSinceLastSwing + HALF_TICK) / cooldownSeconds, 0.0, 1.0));
}

inline float damage(float baseDamage, float strength, bool falling) {
    float d = baseDamage * (0.2f + strength * strength * 0.8f);
    if (falling && strength > 0.9f) d *= 1.5f;
    return d;
}

} // namespace melee
