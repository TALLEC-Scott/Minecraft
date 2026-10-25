#include "mobs/zombie.h"

#include <cmath>
#include <memory>

#include <glm/gtc/constants.hpp>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 SKIN(0.36f, 0.55f, 0.29f);       // zombie green
const glm::vec3 SKIN_DARK(0.20f, 0.33f, 0.16f);  // nose / brow shading
const glm::vec3 EYE(0.06f, 0.08f, 0.06f);
const glm::vec3 MOUTH(0.14f, 0.22f, 0.11f);
const glm::vec3 SHIRT(0.10f, 0.62f, 0.64f);      // cyan
const glm::vec3 PANTS(0.27f, 0.26f, 0.62f);      // blue
const glm::vec3 SHOE(0.30f, 0.30f, 0.34f);

// Chase goal that only locks on to a player the zombie can actually see
// (Minecraft's NearestAttackableTargetGoal mustSee); once chasing it keeps
// following around corners.
class ZombieAttackGoal : public ChaseAndMeleeGoal {
  public:
    using ChaseAndMeleeGoal::ChaseAndMeleeGoal;
    bool canStart(Mob& mob, MobContext& ctx) override {
        return ChaseAndMeleeGoal::canStart(mob, ctx) && mobai::hasLineOfSight(ctx, mob.eyePos(), ctx.playerEye);
    }
};

struct ZombieModel {
    MobModel model;
    int head, rightArm, leftArm, rightLeg, leftLeg;
};

// Minecraft HumanoidModel (16 px = 1 block, +Z forward, feet origin):
// 8×12×4 torso from y 12..24 px, 8³ head on the neck pivot (y 24), 4×12×4
// arms from shoulder pivots at (±5, 22), 4×12×4 legs from hips at
// (±1.9, 12). The mob's right side is -X.
const ZombieModel& zombieModel() {
    static const ZombieModel zm = [] {
        ZombieModel z;
        MobModel& m = z.model;
        m.add({"body", -1, {0, 24 * PX, 0}, {0, -6 * PX, 0}, {4 * PX, 6 * PX, 2 * PX}, SHIRT});

        z.head = m.add({"head", -1, {0, 24 * PX, 0}, {0, 4 * PX, 0}, {4 * PX, 4 * PX, 4 * PX}, SKIN});
        // Face details just proud of the front face (z = +4 px).
        auto face = [&](const char* name, float x, float y, float hw, float hh, glm::vec3 c) {
            m.add({name, z.head, {x * PX, y * PX, 4.05f * PX}, {0, 0, 0}, {hw * PX, hh * PX, 0.05f * PX}, c});
        };
        face("brow", 0, 4.5f, 3.0f, 0.5f, SKIN_DARK);
        face("eye_r", -2, 3.5f, 1.0f, 0.5f, EYE);
        face("eye_l", 2, 3.5f, 1.0f, 0.5f, EYE);
        face("nose", 0, 2.5f, 1.0f, 0.5f, SKIN_DARK);
        face("mouth", 0, 1.0f, 2.0f, 0.5f, MOUTH);

        auto arm = [&](const char* name, float side) {
            int idx = m.add({name, -1, {side * 5 * PX, 22 * PX, 0}, {side * 1 * PX, -4 * PX, 0},
                             {2 * PX, 6 * PX, 2 * PX}, SKIN});
            // Short shirt sleeve over the top third of the arm.
            m.add({"sleeve", idx, {0, 0, 0}, {side * 1 * PX, 0, 0}, {2.05f * PX, 2.05f * PX, 2.05f * PX}, SHIRT});
            return idx;
        };
        z.rightArm = arm("arm_r", -1.0f);
        z.leftArm = arm("arm_l", 1.0f);

        auto leg = [&](const char* name, float x) {
            int idx = m.add({name, -1, {x * PX, 12 * PX, 0}, {0, -6 * PX, 0}, {2 * PX, 6 * PX, 2 * PX}, PANTS});
            m.add({"shoe", idx, {0, -11 * PX, 0}, {0, 0, 0}, {2.05f * PX, 1.05f * PX, 2.05f * PX}, SHOE});
            return idx;
        };
        z.rightLeg = leg("leg_r", -1.9f);
        z.leftLeg = leg("leg_l", 1.9f);

        return z;
    }();
    return zm;
}

} // namespace

Zombie::Zombie() : UndeadMob(MobType::Zombie) {
    goals.add(2, std::make_unique<ZombieAttackGoal>(CHASE_SPEED, ATTACK_DAMAGE, FOLLOW_RANGE, ATTACK_COOLDOWN));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(8.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Zombie::model() const {
    return zombieModel().model;
}

void Zombie::aiTick(MobContext& ctx) {
    Mob::aiTick(ctx);
    aggressive = goals.isRunning<ChaseAndMeleeGoal>();
}

MobPose Zombie::pose(float partialTick) const {
    const ZombieModel& zm = zombieModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseBipedLegs(p, zm.leftLeg, zm.rightLeg, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, zm.head, relYaw, renderHeadPitch(partialTick));

    // Minecraft AnimationUtils.animateZombieArms. X rotations carry over
    // unchanged; Y/Z flip sign (Minecraft's model space is Y-down, -Z
    // forward — a 180° turn about X from ours).
    constexpr float PI = glm::pi<float>();
    float attack = armSwingProgress(partialTick);
    float f = std::sin(attack * PI);
    float f1 = std::sin((1.0f - (1.0f - attack) * (1.0f - attack)) * PI);
    float raise = -PI / (aggressive ? 1.5f : 2.25f); // straight out, higher while chasing
    float spread = 0.1f - f * 0.6f;
    p.rotation[zm.rightArm] = {raise + f * 1.2f - f1 * 0.4f, spread, 0.0f};
    p.rotation[zm.leftArm] = {raise + f * 1.2f - f1 * 0.4f, -spread, 0.0f};

    // Idle bob (AnimationUtils.bobArms): slow outward sway + nod.
    float age = static_cast<float>(ageTicks) + partialTick;
    float sway = std::cos(age * 0.09f) * 0.05f + 0.05f;
    float nod = std::sin(age * 0.067f) * 0.05f;
    p.rotation[zm.rightArm].z -= sway;
    p.rotation[zm.leftArm].z += sway;
    p.rotation[zm.rightArm].x += nod;
    p.rotation[zm.leftArm].x -= nod;

    return p;
}
