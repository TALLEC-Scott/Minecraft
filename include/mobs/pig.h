#pragma once

#include "mob.h"

// Reference passive mob. Quadruped, 10 HP, wanders + looks at the player,
// panics when hurt. Spawns in groups of 2-4 on plains/forest grass.
// Model follows Minecraft's PigModel (16 px = 1 block), facing +Z.
class Pig : public Mob {
  public:
    static constexpr float WANDER_SPEED = 0.09f; // blocks / tick (~1.8 b/s)
    static constexpr float PANIC_SPEED = 0.16f;

    Pig();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    float eyeHeight() const override { return 0.75f; }
};
