#pragma once

// Base class for every living mob (Pig, Zombie, …). Owns the shared
// per-tick simulation:
//   - Minecraft-style physics in blocks/tick @ 20 TPS: gravity 0.08 with
//     0.98 drag, 0.546 ground slipperiness / 0.91 air, 0.42 jump
//   - voxel collision for an arbitrary hitbox (collision.h AABB helpers)
//   - auto step-up: jumps when walking into a 1-block ledge
//   - swimming: floats upward in water
//   - damage, knockback, invulnerability frames, red hurt flash, fall
//     damage, 20-tick death tip-over then removal
//   - limb-swing accumulator for walk animation
// Subclasses supply a model + pose and register AI goals; see
// mob_registry.h for the "how to add a mob" recipe and src/mobs/pig.cpp
// for the reference implementation. GL-free — rendering lives in
// MobRenderer.
//
// Conventions:
//   pos       — FEET center (bottom-center of the hitbox), world units
//   yaw       — degrees, camera convention: forward = (cos yaw, 0, sin yaw)
//               (0 = +X, 90 = +Z). Models face +Z; the renderer rotates.
//   headPitch — degrees, + = looking up

#include <algorithm>
#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "mob_ai.h"
#include "mob_context.h"
#include "mob_model.h"
#include "mob_registry.h"

class Mob {
  public:
    // --- Physics constants (per tick; names avoid camera.h's GRAVITY /
    // JUMP_VELOCITY macros) ---
    static constexpr float TICK_GRAVITY = 0.08f;
    static constexpr float AIR_DRAG = 0.98f; // vertical retention per tick
    static constexpr float GROUND_SLIP = 0.546f; // horizontal retention on ground (0.6 * 0.91)
    static constexpr float AIR_SLIP = 0.91f;     // horizontal retention airborne
    static constexpr float WATER_SLIP = 0.8f;    // all-axis retention in water
    static constexpr float WATER_GRAVITY = 0.02f;
    static constexpr float SWIM_UP = 0.04f;
    static constexpr float JUMP_SPEED = 0.42f; // apex ~1.25 blocks
    static constexpr float MAX_STEP_MOVE = 0.45f; // sub-step horizontal moves longer than this
    static constexpr float CLIMB_SPEED = 0.2f;    // upward speed while wall-climbing (climbsWalls())
    // --- Combat constants ---
    static constexpr int HURT_FLASH_TICKS = 10;
    static constexpr int INVULNERABLE_TICKS = 10;
    static constexpr int DEATH_TICKS = 20;
    static constexpr float KNOCKBACK = 0.4f;
    static constexpr float SAFE_FALL = 3.0f; // blocks before fall damage
    static constexpr int ARM_SWING_TICKS = 6;

    explicit Mob(MobType type);
    virtual ~Mob() = default;
    Mob(const Mob&) = delete;
    Mob& operator=(const Mob&) = delete;

    // --- Identity ---
    const MobType type;
    uint32_t id = 0; // assigned by MobManager (unique per session)
    const MobInfo& info() const { return mobInfo(type); }
    MobCategory category() const { return info().category; }

    // --- Kinematic state ---
    glm::vec3 pos{0.0f};      // feet center
    glm::vec3 prevPos{0.0f};  // pos at the start of the last tick (render interpolation)
    glm::vec3 velocity{0.0f}; // blocks / tick
    float yaw = 0.0f, prevYaw = 0.0f;
    float headYaw = 0.0f, prevHeadYaw = 0.0f; // absolute, same convention as yaw
    float headPitch = 0.0f, prevHeadPitch = 0.0f;
    float halfWidth, height;
    bool onGround = false;
    bool inWater = false;
    bool inLava = false;
    static constexpr float LAVA_DAMAGE = 4.0f;

    // --- Fire (Minecraft: lava sets any mob alight for 15 s; burning deals
    // 1 damage per second; water puts it out) ---
    static constexpr int LAVA_FIRE_TICKS = 300;
    static constexpr int BURN_INTERVAL = 20; // ticks between fire damage
    static constexpr float BURN_DAMAGE = 1.0f;
    int fireTicks = 0; // > 0 while on fire (counts down)
    bool burning() const { return fireTicks > 0; }
    void setOnFire(int ticks) { fireTicks = std::max(fireTicks, ticks); }
    bool horizontalCollision = false;
    float fallDistance = 0.0f;

    // --- Health / combat ---
    float health, maxHealth;
    int hurtTicks = 0;        // > 0 → red flash (counts down from HURT_FLASH_TICKS)
    int invulnerableTicks = 0;
    int deathTicks = 0;       // counts up once dying
    bool dying = false;
    bool removed = false;     // MobManager erases removed mobs
    bool hurtSinceLastAi = false; // set by damage(), cleared after the next AI pass
    glm::vec3 lastHurtFrom{0.0f};
    int armSwingTicks = 0;    // > 0 while an attack swing animates
    int ageTicks = 0;
    bool persistent = false;  // never natural-despawn (debug-spawned hostiles stay too)
    // Sound events raised this tick (bit = MobSoundEvent). MobManager plays
    // and clears them; mob code can also call ctx.sound() directly.
    uint8_t pendingSounds = 0;
    void queueSound(MobSoundEvent ev) { pendingSounds |= static_cast<uint8_t>(1u << static_cast<unsigned>(ev)); }

    // --- Animation ---
    float walkAnimPos = 0.0f, walkAnimSpeed = 0.0f;
    float prevWalkAnimPos = 0.0f, prevWalkAnimSpeed = 0.0f;

    // --- AI ---
    GoalSelector goals;

    // --- Steering API (called by goals) ---
    // Walk toward `target` (feet position) at `speed` blocks/tick. When
    // `avoidDrops` is set the mob halts instead of walking off ledges higher
    // than 3 blocks or into water.
    void navigateTo(glm::vec3 target, float speed, bool avoidDrops = true);
    void stopNavigation();
    bool isNavigating() const { return hasMoveTarget; }
    // Horizontal distance from feet to the current move target (0 if none).
    float distanceToMoveTarget() const;
    // Ticks the current navigation has made (almost) no progress.
    int navStuckTicks() const { return stuckTicks; }
    void lookAt(glm::vec3 point, float maxYawStep = 10.0f, float maxPitchStep = 10.0f);
    void jump() { wantJump = true; }
    void swingArm() { armSwingTicks = ARM_SWING_TICKS; }

    glm::vec3 forward() const;
    glm::vec3 eyePos() const { return pos + glm::vec3(0.0f, eyeHeight(), 0.0f); }
    glm::vec3 center() const { return pos + glm::vec3(0.0f, height * 0.5f, 0.0f); }
    bool isAlive() const { return !dying && !removed; }

    // --- Simulation ---
    // One 20-TPS tick: AI (if alive) → physics → timers. Calls the hooks.
    void tick(MobContext& ctx);

    // Apply damage from `sourcePos` (attacker position, used for knockback
    // direction). Returns false if ignored (invulnerable or already dying).
    // Knockback strength scales by `knockbackScale` (0 = none).
    bool damage(float amount, glm::vec3 sourcePos, float knockbackScale = 1.0f);
    void kill() { damage(health + 1000.0f, pos, 0.0f); }

    // Ray vs. this mob's hitbox. Returns entry distance in `outT`.
    bool rayHit(glm::vec3 origin, glm::vec3 dir, float maxDist, float& outT) const;

    // --- Rendering helpers ---
    glm::vec3 renderPos(float partialTick) const { return glm::mix(prevPos, pos, partialTick); }
    float renderYaw(float partialTick) const;
    float renderHeadYaw(float partialTick) const;
    float renderHeadPitch(float partialTick) const;
    // Interpolated limb swing: x = animPos, y = animSpeed (feed poseXxxLegs).
    glm::vec2 walkAnim(float partialTick) const;
    // Death tip-over progress 0..1.
    float deathProgress(float partialTick) const;
    // 0..1 strength of the arm-swing attack animation.
    float armSwingProgress(float partialTick) const;
    // Flame licks drawn around a burning mob, sized to its hitbox (feet
    // origin, model frame: +Z forward). MobRenderer draws them emissive.
    struct FlameBox {
        glm::vec3 center, halfSize, color;
    };
    static constexpr int FLAME_COUNT = 6;
    std::array<FlameBox, FLAME_COUNT> flameBoxes(float partialTick) const;

    // --- Per-type hooks ---
    virtual const MobModel& model() const = 0;
    virtual MobPose pose(float partialTick) const = 0;
    virtual float eyeHeight() const { return height * 0.85f; }
    // Default: run the goal selector. Override to add per-tick logic, but
    // call Mob::aiTick(ctx) to keep the goals running.
    virtual void aiTick(MobContext& ctx);
    // Runs every tick after physics, also while dying (e.g. burning).
    virtual void customTick(MobContext& /*ctx*/) {}
    virtual void onHurt(float /*amount*/, glm::vec3 /*sourcePos*/) {}
    virtual void onDeath() {}
    virtual float fallDamageMultiplier() const { return 1.0f; }
    virtual bool floatsInWater() const { return true; }
    // Spiders: walking into a wall climbs it at CLIMB_SPEED.
    virtual bool climbsWalls() const { return false; }
    // Ticks between ambient sounds on average (0 = silent).
    virtual int ambientSoundInterval() const { return 120; }

  protected:
    // Movement intent written by goals, consumed by physics.
    bool hasMoveTarget = false;
    glm::vec3 moveTarget{0.0f};
    float moveSpeed = 0.0f;
    bool avoidDrops = true;
    bool wantJump = false;
    int stuckTicks = 0;
    int ambientCooldown = -1; // < 0 → randomized on first tick
    bool lookedThisTick = false;
    float stepDistance = 0.0f;

    void physicsTick(MobContext& ctx);
    // Lava ignites, water extinguishes, then the fire deals its damage.
    void fireTick();
    int burnTimer = 0; // ticks until the next fire damage
    // Desired horizontal velocity from the navigation target (zero if none).
    glm::vec3 steeringVelocity(MobContext& ctx);
    // Move by `delta` with collision; updates onGround / collision flags.
    void moveWithCollision(MobContext& ctx, glm::vec3 delta);
    bool dropAhead(MobContext& ctx, glm::vec3 dir) const;
    void updateWater(MobContext& ctx);
};

// Angle helpers (degrees).
float wrapDegrees(float deg);
float approachAngle(float from, float to, float maxStep);
float yawFromDirection(float dx, float dz);
