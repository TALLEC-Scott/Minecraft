#pragma once

// Reusable AI goals + a Minecraft-style priority selector.
//
// A mob owns a GoalSelector (`Mob::goals`). Each goal claims one or more
// control channels (MOVE, LOOK). Every tick the selector:
//   1. stops running goals whose shouldContinue() returns false,
//   2. starts idle goals whose canStart() returns true, provided every
//      channel they need is free or held by a LESS important goal (larger
//      priority number), which is then interrupted,
//   3. ticks all running goals.
// Lower priority number = more important. Typical passive layout:
//   0 Panic · 5 Wander · 6 LookAtPlayer · 7 RandomLook
// Typical hostile layout:
//   2 ChaseAndMelee · 5 Wander · 6 LookAtPlayer · 7 RandomLook
//
// Goals drive the mob only through its steering API (Mob::navigateTo /
// stopNavigation / lookAt / jump) — physics happens afterwards in
// Mob::tick. Write new goals (ranged attack, creeper swell, …) by
// subclassing MobGoal in the mob's own files.

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

class Mob;
struct MobContext;

enum GoalChannel : uint8_t {
    GOAL_MOVE = 1u << 0,
    GOAL_LOOK = 1u << 1,
};

class MobGoal {
  public:
    explicit MobGoal(uint8_t channels) : channels(channels) {}
    virtual ~MobGoal() = default;

    virtual bool canStart(Mob& mob, MobContext& ctx) = 0;
    virtual bool shouldContinue(Mob& mob, MobContext& ctx) { return canStart(mob, ctx); }
    virtual void start(Mob& /*mob*/, MobContext& /*ctx*/) {}
    virtual void stop(Mob& /*mob*/, MobContext& /*ctx*/) {}
    virtual void tick(Mob& mob, MobContext& ctx) = 0;

    uint8_t channels;
};

class GoalSelector {
  public:
    void add(int priority, std::unique_ptr<MobGoal> goal);
    void tick(Mob& mob, MobContext& ctx);
    // Stop everything (used on death).
    void stopAll(Mob& mob, MobContext& ctx);
    // True if a goal of this dynamic type is currently running.
    template <typename T> bool isRunning() const {
        for (const auto& e : entries)
            if (e.running && dynamic_cast<const T*>(e.goal.get())) return true;
        return false;
    }
    bool empty() const { return entries.empty(); }

  private:
    struct Entry {
        int priority;
        std::unique_ptr<MobGoal> goal;
        bool running = false;
    };
    std::vector<Entry> entries; // kept sorted by priority
};

// --- Stock goals -------------------------------------------------------------

// Stroll to a random reachable spot within `radius` blocks, pausing between
// walks. `chancePerTick` ~ 1/120 gives Minecraft's lazy amble.
class WanderGoal : public MobGoal {
  public:
    explicit WanderGoal(float speed, float radius = 10.0f, float chancePerTick = 1.0f / 120.0f)
        : MobGoal(GOAL_MOVE), speed(speed), radius(radius), chancePerTick(chancePerTick) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void stop(Mob& mob, MobContext& ctx) override;
    void tick(Mob& /*mob*/, MobContext& /*ctx*/) override { ++ticksRunning; }

  private:
    float speed, radius, chancePerTick;
    glm::vec3 target{0.0f};
    int ticksRunning = 0;
};

// Passive mobs: after taking damage run fast in random directions (biased
// away from the attacker) for `durationTicks` (~5 s).
class PanicGoal : public MobGoal {
  public:
    explicit PanicGoal(float speed, int durationTicks = 100) : MobGoal(GOAL_MOVE), speed(speed), duration(durationTicks) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void stop(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

  private:
    float speed;
    int duration;
    int remaining = 0;
    void pickTarget(Mob& mob, MobContext& ctx);
};

// Turn the head toward the player while they're within `range` blocks.
class LookAtPlayerGoal : public MobGoal {
  public:
    explicit LookAtPlayerGoal(float range = 8.0f, float chancePerTick = 0.02f)
        : MobGoal(GOAL_LOOK), range(range), chancePerTick(chancePerTick) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

  private:
    float range, chancePerTick;
    int remaining = 0;
};

// Idle head turns when nothing else wants the LOOK channel.
class RandomLookGoal : public MobGoal {
  public:
    RandomLookGoal() : MobGoal(GOAL_LOOK) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& /*mob*/, MobContext& /*ctx*/) override { return remaining > 0; }
    void start(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

  private:
    float lookYaw = 0.0f; // absolute yaw to look toward
    int remaining = 0;
};

// Hostile melee: target the player within `followRange`, steer toward them
// (no cliff avoidance) and hit when in reach every `cooldownTicks`.
// Reach follows Minecraft: (mob width * 2)^2 + player width (squared blocks).
// Subclass and override onAttack() for arm-swing / special effects.
class ChaseAndMeleeGoal : public MobGoal {
  public:
    ChaseAndMeleeGoal(float speed, float damage, float followRange = 16.0f, int cooldownTicks = 20)
        : MobGoal(GOAL_MOVE | GOAL_LOOK), speed(speed), damage(damage), followRange(followRange),
          cooldown(cooldownTicks) {}
    bool canStart(Mob& mob, MobContext& ctx) override;
    bool shouldContinue(Mob& mob, MobContext& ctx) override;
    void start(Mob& mob, MobContext& ctx) override;
    void stop(Mob& mob, MobContext& ctx) override;
    void tick(Mob& mob, MobContext& ctx) override;

    // Squared horizontal reach for `mob` against the player.
    static float reachSq(const Mob& mob);

  protected:
    // Called the tick a hit lands (after damagePlayer). Default: Mob::swingArm().
    virtual void onAttack(Mob& mob, MobContext& ctx);

    float speed, damage, followRange;
    int cooldown;
    int cooldownLeft = 0;
    int repathTicks = 0;
};

// --- Steering / terrain helpers shared by goals ----------------------------

namespace mobai {
// Find a standable feet position near (x, z): scans dy in [+up, -down]
// around `nearY` for a solid floor with room for `height` blocks of
// non-solid space. Returns false if none (or the column is unloaded).
bool findStandY(const MobContext& ctx, int x, int z, int nearY, float halfWidth, float height, int up, int down,
                glm::vec3& outFeet);
// Random wander destination within `radius`. Rejects water floors when
// `avoidWater`. Returns false after a few failed attempts.
bool randomStandTarget(Mob& mob, MobContext& ctx, float radius, bool avoidWater, glm::vec3& out,
                       glm::vec3 awayFrom = glm::vec3(0.0f), bool biasAway = false);
} // namespace mobai
