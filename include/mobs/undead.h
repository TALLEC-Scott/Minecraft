#pragma once

#include <array>

#include "mob.h"

// Shared base for the undead hostiles (Zombie, Skeleton): Minecraft's
// sunlight burning. While it's day and the block at the mob's eyes sees the
// open sky (stored sky light 15) the mob is set on fire for 8 s; burning
// deals 1 damage every second, and water puts it out immediately. Also
// owns a small flame overlay (hidden boxes shown while burning) that each
// model adds with addFlameParts() and poses with poseFlames().
class UndeadMob : public Mob {
  public:
    static constexpr int SUN_FIRE_TICKS = 160; // Minecraft setSecondsOnFire(8)
    static constexpr int BURN_INTERVAL = 20;   // ticks between fire damage
    static constexpr float BURN_DAMAGE = 1.0f;
    static constexpr int FLAME_COUNT = 6;

    explicit UndeadMob(MobType type) : Mob(type) {}

    int fireTicks = 0; // > 0 while on fire (counts down)
    bool burning() const { return fireTicks > 0; }
    // Day, not in water, and the eye block has full (unshaded) sky light.
    bool sunExposed(const MobContext& ctx) const;

    void customTick(MobContext& ctx) override;
    float eyeHeight() const override { return 1.74f; } // Minecraft zombie / skeleton

    using FlameParts = std::array<int, FLAME_COUNT>;
    // Append the flame boxes (root-attached, hidden unless burning).
    static FlameParts addFlameParts(MobModel& m);
    // Show + flicker the flames while burning, hide them otherwise.
    void poseFlames(MobPose& pose, const FlameParts& flames, float partialTick) const;

  private:
    int burnTimer = 0; // ticks until the next fire damage
};
