#pragma once

#include "mobs/cow.h"

// Cow-rain novelty mob: a creeper-green cow that explodes (power 3, same
// as a creeper) the moment it lands after falling. Never spawns naturally
// (no spawn biomes in its registry row) — only MobManager's cow rain drops
// it. Keeps the cow's wander/panic AI if it somehow survives on the ground,
// e.g. when debug-spawned (F6) without a drop.
class CreeperCow : public Cow {
  public:
    static constexpr float EXPLOSION_POWER = 3.0f;
    // Airborne ticks required before a landing counts as an impact, so a
    // cow hopping up a 1-block step doesn't blow up.
    static constexpr int MIN_FALL_TICKS = 5;

    CreeperCow();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    void customTick(MobContext& ctx) override;
    float fallDamageMultiplier() const override { return 0.0f; } // the explosion is the landing

  private:
    int airTicks = 0;
};
