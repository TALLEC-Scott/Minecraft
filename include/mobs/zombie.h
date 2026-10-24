#pragma once

#include "mobs/undead.h"

// Hostile melee undead. 20 HP, chases the player it can see within 35
// blocks and hits for 3 (Normal difficulty) once a second; burns in
// sunlight (UndeadMob). Model follows Minecraft's HumanoidModel with the
// zombie arm pose: arms held straight out, raised higher while chasing.
class Zombie : public UndeadMob {
  public:
    static constexpr float WANDER_SPEED = 0.08f; // blocks / tick
    // Minecraft movement attribute 0.23 ≈ 2.3 blocks/s — slower than a
    // walking player (4.3 b/s) so you can always back away.
    static constexpr float CHASE_SPEED = 0.115f;
    static constexpr float ATTACK_DAMAGE = 3.0f;
    static constexpr float FOLLOW_RANGE = 35.0f;
    static constexpr int ATTACK_COOLDOWN = 20;

    Zombie();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    void aiTick(MobContext& ctx) override;

    bool aggressive = false; // chase goal running → arms raised higher
};
