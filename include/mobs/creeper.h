#pragma once

#include "mob.h"

// Hostile. 20 HP, 0.6 × 1.7. Walks up to the player and, once within
// 3 blocks with line of sight, hisses and swells for 30 ticks (1.5 s)
// before exploding (power 3) — it removes itself, no death animation.
// Backing off past 7 blocks makes it deflate. Never burns in daylight.
// Model follows Minecraft's CreeperModel (16 px = 1 block), facing +Z.
class Creeper : public Mob {
  public:
    static constexpr float CHASE_SPEED = 0.125f; // Minecraft speed attr 0.25 (~2.5 b/s)
    static constexpr float WANDER_SPEED = 0.08f;
    static constexpr int FUSE_TICKS = 30;
    static constexpr float IGNITE_RANGE = 3.0f;  // start swelling inside this
    static constexpr float DEFUSE_RANGE = 7.0f;  // deflate beyond this
    static constexpr float EXPLOSION_POWER = 3.0f;

    Creeper();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    void customTick(MobContext& ctx) override;
    int ambientSoundInterval() const override { return 0; } // creepers are silent until they hiss

    // Written by the swell goal: +1 = swelling, -1 = deflating.
    void setSwellDir(int dir) { swellDir = dir; }
    int swellTicks() const { return swell; }
    // Fuse progress 0..1 (Minecraft Creeper.getSwelling), interpolated.
    float swelling(float partialTick) const;

  private:
    int swell = 0, prevSwell = 0;
    int swellDir = -1;
};
