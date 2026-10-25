#pragma once

#include "mob.h"

// Shared base for the undead hostiles (Zombie, Skeleton): Minecraft's
// sunlight burning. While it's day and the block at the mob's eyes sees the
// open sky (stored sky light 15) the mob is set on fire for 8 s. The fire
// itself (damage, water, flames) is Mob's, shared with lava.
class UndeadMob : public Mob {
  public:
    static constexpr int SUN_FIRE_TICKS = 160; // Minecraft setSecondsOnFire(8)

    explicit UndeadMob(MobType type) : Mob(type) {}

    // Day, not in water, and the eye block has full (unshaded) sky light.
    bool sunExposed(const MobContext& ctx) const;

    void customTick(MobContext& ctx) override;
    float eyeHeight() const override { return 1.74f; } // Minecraft zombie / skeleton
};
