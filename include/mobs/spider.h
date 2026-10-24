#pragma once

#include "mob.h"

// Hostile. 16 HP, 1.4 × 0.9. Climbs walls, leaps at its target from 2-4
// blocks away, bites for 2. Only picks fights in the dark (light <= 7)
// or after being hurt; in bright light it wanders, and an ongoing chase
// has a 1 %/tick chance to lose interest (Minecraft SpiderAttackGoal).
// Model follows Minecraft's SpiderModel (16 px = 1 block), facing +Z.
class Spider : public Mob {
  public:
    static constexpr float CHASE_SPEED = 0.15f; // Minecraft speed attr 0.3
    static constexpr float WANDER_SPEED = 0.09f;
    static constexpr float ATTACK_DAMAGE = 2.0f;
    static constexpr int ATTACK_COOLDOWN = 20;
    static constexpr int MAX_HOSTILE_LIGHT = 7;
    static constexpr int PROVOKED_TICKS = 400; // stays hostile this long after a hit
    static constexpr float LEAP_MIN_DIST = 2.0f;
    static constexpr float LEAP_MAX_DIST = 4.0f;
    static constexpr float LEAP_CHANCE = 0.1f; // per tick while in leap range
    static constexpr float LEAP_HORIZONTAL = 0.4f;
    static constexpr float LEAP_UP = 0.4f;

    Spider();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    float eyeHeight() const override { return 0.65f; }
    bool climbsWalls() const override { return true; }
    void aiTick(MobContext& ctx) override;
    void onHurt(float amount, glm::vec3 sourcePos) override;

    // Light at the spider's eyes is dark enough to turn hostile.
    bool inDarkness(const MobContext& ctx) const;
    bool isProvoked() const { return provokedTicks > 0; }
    // True while the attack goal is chasing the player (updated each AI tick).
    bool isAttacking() const { return attacking; }
    // Pounce toward `target` (feet): 0.4 horizontal + 0.4 up.
    void leapToward(glm::vec3 target);
    int leapCount() const { return leaps; }

  private:
    int provokedTicks = 0;
    bool attacking = false;
    int leaps = 0;
};
