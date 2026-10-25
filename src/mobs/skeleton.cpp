#include "mobs/skeleton.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

#include <glm/gtc/constants.hpp>

// ---------------------------------------------------------------------------
// RangedBowAttackGoal
// ---------------------------------------------------------------------------

namespace {
constexpr int SEE_TIME_TO_STRAFE = 20;   // ticks of sight before stopping to strafe
constexpr int STRAFE_REROLL_TICKS = 20;  // ticks between strafe direction re-rolls
constexpr float STRAFE_FLIP_CHANCE = 0.3f;
constexpr int LOST_SIGHT_TICKS = 60;     // stop drawing after this long unseen
constexpr int GIVE_UP_TICKS = 100;       // drop the target after this long unseen
constexpr float STRAFE_SPEED_SCALE = 0.7f; // Minecraft strafe(±0.5, ±0.5) → |v| ≈ 0.71
} // namespace

bool RangedBowAttackGoal::canStart(Mob& mob, MobContext& ctx) {
    if (!ctx.playerTargetable) return false;
    if (glm::distance(mob.eyePos(), ctx.playerEye) >= followRange) return false;
    return mobai::hasLineOfSight(ctx, mob.eyePos(), ctx.playerEye);
}

bool RangedBowAttackGoal::shouldContinue(Mob& mob, MobContext& ctx) {
    if (!ctx.playerTargetable) return false;
    return glm::distance(mob.eyePos(), ctx.playerEye) < followRange * 1.5f && seeTime > -GIVE_UP_TICKS;
}

void RangedBowAttackGoal::start(Mob& /*mob*/, MobContext& /*ctx*/) {
    running = true;
    seeTime = 0;
    strafingTime = -1;
}

void RangedBowAttackGoal::stop(Mob& mob, MobContext& /*ctx*/) {
    running = false;
    seeTime = 0;
    attackTime = -1;
    useTicks = -1;
    mob.stopNavigation();
}

void RangedBowAttackGoal::tick(Mob& mob, MobContext& ctx) {
    bool canSee = mobai::hasLineOfSight(ctx, mob.eyePos(), ctx.playerEye);
    if (canSee != (seeTime > 0)) seeTime = 0;
    seeTime += canSee ? 1 : -1;

    glm::vec3 toPlayer = ctx.playerFeet - mob.pos;
    float distSq = glm::dot(toPlayer, toPlayer);

    // In range and seen for a second: hold ground and strafe; otherwise
    // walk toward the player (greedy steering, won't walk off cliffs).
    if (distSq <= attackRangeSq && seeTime >= SEE_TIME_TO_STRAFE) {
        mob.stopNavigation();
        ++strafingTime;
    } else {
        mob.navigateTo(ctx.playerFeet, speed, true);
        strafingTime = -1;
    }

    if (strafingTime >= STRAFE_REROLL_TICKS) {
        if (ctx.chance(STRAFE_FLIP_CHANCE)) strafeClockwise = !strafeClockwise;
        if (ctx.chance(STRAFE_FLIP_CHANCE)) strafeBackwards = !strafeBackwards;
        strafingTime = 0;
    }
    if (strafingTime > -1) {
        if (distSq > attackRangeSq * 0.75f)
            strafeBackwards = false;
        else if (distSq < BACK_OFF_RANGE * BACK_OFF_RANGE)
            strafeBackwards = true;
        glm::vec2 to(toPlayer.x, toPlayer.z);
        float len = glm::length(to);
        to = len > 1e-4f ? to / len : glm::vec2(1.0f, 0.0f);
        glm::vec2 side(-to.y, to.x);
        glm::vec2 dir = glm::normalize(to * (strafeBackwards ? -0.5f : 0.5f) + side * (strafeClockwise ? 0.5f : -0.5f));
        mob.navigateTo(mob.pos + glm::vec3(dir.x, 0.0f, dir.y) * 2.0f, speed * STRAFE_SPEED_SCALE, true);
    }
    mob.lookAt(ctx.playerEye, 30.0f, 30.0f);

    if (useTicks >= 0) {
        ++useTicks;
        if (!canSee && seeTime < -LOST_SIGHT_TICKS) {
            useTicks = -1; // lost the target: lower the bow
        } else if (canSee && useTicks >= DRAW_TICKS) {
            fire(mob, ctx);
            useTicks = -1;
            attackTime = ctx.randInt(minInterval, maxInterval);
        }
    } else if (--attackTime <= 0 && seeTime >= -LOST_SIGHT_TICKS) {
        useTicks = 0; // start drawing
    }
}

glm::vec3 RangedBowAttackGoal::launchPos(const Mob& mob) {
    return mob.eyePos() - glm::vec3(0.0f, 0.1f, 0.0f);
}

glm::vec3 RangedBowAttackGoal::aimVelocity(glm::vec3 from, glm::vec3 targetFeet, float speed) {
    // Minecraft AbstractSkeleton.performRangedAttack: aim at a third of the
    // target's height, lifted by 0.2 per block of horizontal distance to
    // cancel the arrow's drop.
    glm::vec3 d = targetFeet + glm::vec3(0.0f, AIM_HEIGHT, 0.0f) - from;
    float horiz = std::sqrt(d.x * d.x + d.z * d.z);
    glm::vec3 dir(d.x, d.y + horiz * 0.2f, d.z);
    float len = glm::length(dir);
    if (len < 1e-6f) return glm::vec3(0.0f, 0.0f, speed);
    return dir / len * speed;
}

void RangedBowAttackGoal::fire(Mob& mob, MobContext& ctx) {
    ProjectileSpec spec;
    spec.pos = launchPos(mob);
    // Projectile.shoot: unit direction + gaussian spread, then scale.
    glm::vec3 dir = glm::normalize(aimVelocity(spec.pos, ctx.playerFeet, 1.0f));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    glm::vec3 spread(gauss(*ctx.rng), gauss(*ctx.rng), gauss(*ctx.rng));
    spec.velocity = (dir + spread * (0.0075f * INACCURACY)) * ARROW_SPEED;
    // Arrow damage scales with speed (ceil(|v| * base)), Normal-difficulty
    // base ≈ 2.2 → 3-4 at launch speed, clamped to 2..4.
    float base = 1.75f + ctx.randf() * 0.5f;
    spec.damage = std::clamp(std::ceil(glm::length(spec.velocity) * base), 2.0f, 4.0f);
    spec.shooterId = mob.id;
    spec.hurtsMobs = true; // stray arrows hit other mobs, like Minecraft
    ctx.spawnProjectile(spec);
    mob.queueSound(MobSoundEvent::Attack);
}

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 BONE(0.84f, 0.84f, 0.81f);
const glm::vec3 BONE_JOINT(0.68f, 0.68f, 0.65f);
const glm::vec3 CAVITY(0.24f, 0.24f, 0.23f); // shadowed inside of the ribcage
const glm::vec3 SOCKET(0.10f, 0.10f, 0.10f);
const glm::vec3 BOW(0.48f, 0.32f, 0.16f);
const glm::vec3 BOW_GRIP(0.30f, 0.20f, 0.10f);
const glm::vec3 STRING(0.88f, 0.88f, 0.86f);

constexpr float STRING_PULL = 2.0f * PX; // how far a full draw pulls the string back

struct SkeletonModel {
    MobModel model;
    int head, rightArm, leftArm, rightLeg, leftLeg, bowGrip, bowString;
};

// Minecraft SkeletonModel (16 px = 1 block, +Z forward, feet origin):
// humanoid 8³ head on the neck pivot (y 24), a ribcage in place of the
// solid torso, 2×12×2 arms from shoulders at (±5, 22) and legs from hips
// at (±2, 12). The mob's right side is -X; the bow sits in the right hand.
const SkeletonModel& skeletonModel() {
    static const SkeletonModel sm = [] {
        SkeletonModel s;
        MobModel& m = s.model;

        // Torso (8×12×4 px, neck pivot at y 24): a shadowed rib cavity
        // wrapped by four full-depth ribs, with a sternum in front and the
        // spine behind. Below the ribs only the spine bridges the open
        // waist down to the pelvis, so the silhouette pinches like
        // Minecraft's skeleton from every side.
        int body = m.add({"body", -1, {0, 24 * PX, 0}, {0, -4.5f * PX, 0}, {3.4f * PX, 3.5f * PX, 1.5f * PX}, CAVITY});
        m.add({"spine", body, {0, 0, 0}, {0, -6 * PX, -1.4f * PX}, {0.75f * PX, 6 * PX, 0.6f * PX}, BONE});
        m.add({"sternum", body, {0, 0, 0}, {0, -4.25f * PX, 1.55f * PX}, {0.75f * PX, 3.25f * PX, 0.5f * PX}, BONE});
        m.add({"collarbone", body, {0, 0, 0}, {0, -0.5f * PX, 0}, {4 * PX, 0.5f * PX, 2 * PX}, BONE});
        for (float y : {-2.5f, -4.5f, -6.5f})
            m.add({"rib", body, {0, 0, 0}, {0, y * PX, 0}, {4 * PX, 0.5f * PX, 2 * PX}, BONE});
        m.add({"rib_low", body, {0, 0, 0}, {0, -8.5f * PX, 0}, {3 * PX, 0.5f * PX, 1.8f * PX}, BONE});
        m.add({"pelvis", body, {0, 0, 0}, {0, -11 * PX, 0}, {4 * PX, 1 * PX, 2 * PX}, BONE});

        // Skull: 8³ head with Minecraft's face layout (head-local px, face
        // plane at z = +4) — 2×2 eye sockets in the middle rows, a 2×1
        // nose hole, and a dark mouth band broken up by three teeth.
        s.head = m.add({"head", -1, {0, 24 * PX, 0}, {0, 4 * PX, 0}, {4 * PX, 4 * PX, 4 * PX}, BONE});
        auto face = [&](const char* name, float x, float y, float hw, float hh, glm::vec3 color, float z = 4.05f) {
            m.add({name, s.head, {x * PX, (4 + y) * PX, z * PX}, {0, 0, 0}, {hw * PX, hh * PX, 0.05f * PX}, color});
        };
        face("eye_r", -2, 0.5f, 1.0f, 1.0f, SOCKET);
        face("eye_l", 2, 0.5f, 1.0f, 1.0f, SOCKET);
        face("nose", 0, -1.5f, 1.0f, 0.5f, SOCKET);
        face("mouth", 0, -2.5f, 3.0f, 0.5f, SOCKET);
        for (float x : {-2.0f, 0.0f, 2.0f})
            face("tooth", x, -2.5f, 0.5f, 0.5f, BONE, 4.1f);

        // 2×12×2 px limbs with a slightly darker joint band at the elbow /
        // knee so they read as bones rather than plain sticks.
        auto limb = [&](const char* name, float x, float y, float centerY) {
            int i = m.add({name, -1, {x * PX, y * PX, 0}, {0, centerY * PX, 0}, {1 * PX, 6 * PX, 1 * PX}, BONE});
            m.add({"joint", i, {0, 0, 0}, {0, (centerY - 0.5f) * PX, 0}, {1.12f * PX, 0.6f * PX, 1.12f * PX},
                   BONE_JOINT});
            return i;
        };
        s.rightArm = limb("arm_r", -5, 22, -4);
        s.leftArm = limb("arm_l", 5, 22, -4);
        s.rightLeg = limb("leg_r", -2, 12, -6);
        s.leftLeg = limb("leg_l", 2, 12, -6);

        // Bow in the right hand (arm ends 10 px below the shoulder). Its
        // length runs along the arm's local Z, so it stands upright when
        // the arm is raised forward (local -Y → +Z). Limbs curve back
        // toward the shooter (local +Y); the string spans the tips.
        int grip = m.add({"bow_grip", s.rightArm, {0, -10 * PX, 0}, {0, -1.2f * PX, 0},
                          {0.6f * PX, 0.6f * PX, 1.5f * PX}, BOW_GRIP});
        s.bowGrip = grip;
        for (float zs : {1.0f, -1.0f}) {
            m.add({"bow_limb", grip, {0, 0, 0}, {0, -0.9f * PX, zs * 2.8f * PX}, {0.5f * PX, 0.5f * PX, 1.4f * PX},
                   BOW});
            m.add({"bow_tip", grip, {0, 0, 0}, {0, 0.1f * PX, zs * 5.2f * PX}, {0.5f * PX, 0.5f * PX, 1.1f * PX},
                   BOW});
        }
        s.bowString = m.add({"bow_string", grip, {0, 0.4f * PX, 0}, {0, 0, 0}, {0.15f * PX, 0.15f * PX, 6.1f * PX},
                             STRING});

        return s;
    }();
    return sm;
}

} // namespace

Skeleton::Skeleton() : UndeadMob(MobType::Skeleton) {
    auto bow = std::make_unique<RangedBowAttackGoal>(ATTACK_SPEED);
    bowGoal = bow.get();
    goals.add(4, std::move(bow));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(8.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Skeleton::model() const {
    return skeletonModel().model;
}

void Skeleton::aiTick(MobContext& ctx) {
    Mob::aiTick(ctx);
    aiming = bowGoal->active();
    bowDrawTicks = bowGoal->drawing() ? bowGoal->drawTicks() + 1 : 0;
}

void Skeleton::customTick(MobContext& ctx) {
    UndeadMob::customTick(ctx);
    // Strafing steers the body along the movement direction; keep it
    // squared up to the target instead so the bow points where it shoots.
    if (aiming && !dying) yaw = approachAngle(yaw, headYaw, 45.0f);
}

MobPose Skeleton::pose(float partialTick) const {
    const SkeletonModel& sm = skeletonModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseBipedLegs(p, sm.leftLeg, sm.rightLeg, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, sm.head, relYaw, renderHeadPitch(partialTick));

    constexpr float PI = glm::pi<float>();
    if (aiming && !dying) {
        // Minecraft ArmPose.BOW_AND_ARROW (Y signs flipped into our frame):
        // both arms straight out along the gaze, the left hand angled in
        // to the string.
        glm::vec3 h = p.rotation[sm.head];
        p.rotation[sm.rightArm] = {-PI / 2.0f + h.x, 0.1f + h.y, 0.0f};
        p.rotation[sm.leftArm] = {-PI / 2.0f + h.x, -0.5f + h.y, 0.0f};
        if (bowDrawTicks > 0) {
            float draw = std::min((static_cast<float>(bowDrawTicks - 1) + partialTick) /
                                      static_cast<float>(RangedBowAttackGoal::DRAW_TICKS),
                                  1.0f);
            p.offset[sm.bowString].y += draw * STRING_PULL;
        }
    } else {
        poseBipedArms(p, sm.leftArm, sm.rightArm, anim.x, anim.y);
        // ArmPose.ITEM: the bow hand rides a little forward, and the bow
        // hangs upright at the side (its length runs along the arm's local
        // Z, so tip it back by 90° about X) instead of poking forward.
        p.rotation[sm.rightArm].x = p.rotation[sm.rightArm].x * 0.5f - PI / 10.0f;
        p.rotation[sm.bowGrip].x = -PI / 2.0f;
    }

    return p;
}
