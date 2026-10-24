#pragma once

// Local-player health: 20 half-hearts, invulnerability window after a hit,
// slow regeneration when not recently hurt, death + respawn. Pure logic
// driven by wall-clock seconds so it's unit-testable without GLFW (the
// Player passes glfwGetTime()).

#include <algorithm>
#include <cmath>

class PlayerHealth {
  public:
    static constexpr float MAX_HEALTH = 20.0f;
    static constexpr double INVULNERABLE_SECONDS = 0.5; // Minecraft: 10 ticks
    static constexpr double REGEN_DELAY = 5.0;          // no regen this long after a hit
    static constexpr double REGEN_INTERVAL = 4.0;       // +1 HP per interval
    static constexpr double FLASH_SECONDS = 0.5;        // HUD / screen red flash

    float health = MAX_HEALTH;

    // Apply damage at time `now`. Returns false when ignored (dead, inside
    // the invulnerability window, or non-positive amount).
    bool hurt(float amount, double now) {
        if (isDead() || amount <= 0.0f) return false;
        if (hasBeenHurt && now - lastHurtTime < INVULNERABLE_SECONDS) return false;
        health = std::max(0.0f, health - amount);
        lastHurtTime = now;
        hasBeenHurt = true;
        lastRegenTime = now;
        return true;
    }

    // Regeneration. Call once per frame.
    void update(double now) {
        if (isDead() || health >= MAX_HEALTH) {
            lastRegenTime = now;
            return;
        }
        if (hasBeenHurt && now - lastHurtTime < REGEN_DELAY) {
            lastRegenTime = now;
            return;
        }
        if (now - lastRegenTime >= REGEN_INTERVAL) {
            health = std::min(MAX_HEALTH, health + 1.0f);
            lastRegenTime = now;
        }
    }

    bool isDead() const { return health <= 0.0f; }

    void respawn(double now) {
        health = MAX_HEALTH;
        hasBeenHurt = false;
        lastRegenTime = now;
    }

    // 1 → 0 over FLASH_SECONDS after the last hit (red screen tint, heart blink).
    float hurtFlash(double now) const {
        if (!hasBeenHurt) return 0.0f;
        double t = now - lastHurtTime;
        if (t < 0.0 || t >= FLASH_SECONDS) return 0.0f;
        return static_cast<float>(1.0 - t / FLASH_SECONDS);
    }

    // Minecraft fall damage: ceil(distance - 3).
    static float fallDamage(float fallDistance) { return std::max(0.0f, std::ceil(fallDistance - 3.0f)); }

  private:
    double lastHurtTime = 0.0;
    double lastRegenTime = 0.0;
    bool hasBeenHurt = false;
};
