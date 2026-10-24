#pragma once

#include "mobs/undead.h"

// Minecraft's RangedBowAttackGoal: lock on to a visible player within
// `followRange`, close to `attackRange`, then strafe (random left/right
// switching, backing off when closer than BACK_OFF_RANGE) while drawing
// the bow for DRAW_TICKS and loosing an arrow at the player. Needs line of
// sight to fire; gives up after ~5 s without it. Reusable by any bow mob
// (strays, …) — it only drives the mob through the steering API.
class RangedBowAttackGoal : public MobGoal {
  public:
    static constexpr int DRAW_TICKS = 20;
    static constexpr float ARROW_SPEED = 1.6f;    // blocks / tick at launch
    static constexpr float INACCURACY = 6.0f;     // Normal difficulty (14 - 2 * 4)
    static constexpr float BACK_OFF_RANGE = 5.0f; // strafe backwards when closer
    static constexpr float AIM_HEIGHT = 0.6f;     // a third of the player's height (Minecraft getY(1/3))

    RangedBowAttackGoal(float speed, float attackRange = 15.0f, int minInterval = 20, int maxInterval = 40,
                        float followRange = 16.0f)
        : MobGoal(GOAL_MOVE | GOAL_LOOK), speed(speed), attackRangeSq(attackRange * attackRange),
          minInterval(minInterval), maxInterval(maxInterval), followRange(followRange) {}

    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void stop(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

    bool active() const { return running; }
    bool drawing() const { return useTicks >= 0; }
    int drawTicks() const { return useTicks < 0 ? 0 : useTicks; }

    // Launch velocity from `from` toward a player standing at `targetFeet`
    // with Minecraft's gravity compensation (aim y += horizontal distance
    // * 0.2), no random spread.
    static glm::vec3 aimVelocity(glm::vec3 from, glm::vec3 targetFeet, float speed = ARROW_SPEED);
    // Where arrows leave the shooter (eye height - 0.1, Minecraft).
    static glm::vec3 launchPos(const Mob& mob);

  private:
    float speed, attackRangeSq;
    int minInterval, maxInterval;
    float followRange;
    bool running = false;
    int seeTime = 0;        // + ticks with LOS, - ticks without
    int attackTime = -1;    // cooldown before the next draw starts
    int useTicks = -1;      // ticks the bow has been drawn; -1 = not drawing
    int strafingTime = -1;  // -1 = approaching, else ticks since last strafe re-roll
    bool strafeClockwise = false;
    bool strafeBackwards = false;

    void fire(Mob& mob, MobContext& ctx);
};

// Hostile ranged undead. 20 HP, shoots arrows from up to 15 blocks while
// strafing, burns in sunlight (UndeadMob). Model follows Minecraft's
// SkeletonModel: humanoid head + ribcage torso, 2 px bone limbs, bow in
// the right hand, both arms raised in the bow pose while attacking.
class Skeleton : public UndeadMob {
  public:
    static constexpr float WANDER_SPEED = 0.08f; // blocks / tick
    static constexpr float ATTACK_SPEED = 0.125f;

    Skeleton();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    void aiTick(MobContext& ctx) override;
    void customTick(MobContext& ctx) override;

    bool aiming = false; // bow goal running → bow pose, body faces the target
    int bowDrawTicks = 0;

  private:
    RangedBowAttackGoal* bowGoal = nullptr; // owned by `goals`
};
