#pragma once

#include "mob.h"

// Passive biped, 4 HP. Wanders, looks at the player, panics (fast) when
// hurt. Flutters down instead of falling (no fall damage) and flaps its
// wings while airborne — Minecraft's flap / flapSpeed accumulators. Lays
// an egg every 6000-12000 ticks; with no item system the egg is a small
// cream projectile (no damage) that drops behind the chicken and lingers
// on the ground for Projectile::STUCK_TICKS before vanishing.
// Spawns in groups of 2-4 on plains/forest grass. Model follows
// Minecraft's ChickenModel (16 px = 1 block), facing +Z.
class Chicken : public Mob {
  public:
    static constexpr float WANDER_SPEED = 0.09f; // blocks / tick
    static constexpr float PANIC_SPEED = 0.18f;  // Minecraft: 1.4x walk
    static constexpr float FALL_SLOWDOWN = 0.6f; // vy multiplier per tick while falling
    static constexpr int EGG_MIN_TICKS = 6000;
    static constexpr int EGG_MAX_TICKS = 12000;

    Chicken();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    float eyeHeight() const override { return 0.644f; } // Minecraft: height * 0.92
    float fallDamageMultiplier() const override { return 0.0f; }
    void customTick(MobContext& ctx) override;

    // Wing rotation (radians, outward) for this frame: 0 at rest on the
    // ground, up to ~2 while flapping in the air.
    float wingAngle(float partialTick) const;

    // --- Flap state (Minecraft Chicken.flap / flapSpeed / flapping) ---
    float flap = 0.0f, prevFlap = 0.0f;           // wing phase accumulator
    float flapSpeed = 0.0f, prevFlapSpeed = 0.0f; // 0..1 flap amplitude
    float flapping = 1.0f;                        // phase speed, decays 0.9/tick
    // Ticks until the next egg (< 0 → randomized on the first tick).
    int eggTime = -1;
    int eggsLaid = 0;

  private:
    void layEgg(MobContext& ctx);
};
