#include "mobs/chicken.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 FEATHER(0.97f, 0.97f, 0.95f);
const glm::vec3 WING(0.90f, 0.90f, 0.88f); // a touch darker so the wings read against the body
const glm::vec3 BEAK(0.98f, 0.70f, 0.18f);
const glm::vec3 WATTLE(0.85f, 0.10f, 0.10f);
const glm::vec3 EYE(0.06f, 0.06f, 0.06f);
const glm::vec3 LEG(0.95f, 0.78f, 0.22f);
const glm::vec3 EGG(0.96f, 0.91f, 0.78f);

struct ChickenModel {
    MobModel model;
    int head, leftLeg, rightLeg, leftWing, rightWing;
};

// Built once. Dimensions from Minecraft's ChickenModel converted to
// +Z-forward, feet-origin space: body 6×6×8 px spanning y 5..11, head
// 4×6×3 px (y 9..15) at the front with beak + wattle, 1-px-thick wings
// hanging from shoulders at y = 11 px on each side of the body, thin legs
// from hips at y = 5 px.
const ChickenModel& chickenModel() {
    static const ChickenModel cm = [] {
        ChickenModel c;
        MobModel& m = c.model;
        m.add({"body", -1, {0, 8 * PX, 0}, {0, 0, 0}, {3 * PX, 3 * PX, 4 * PX}, FEATHER});

        // Neck pivot at the bottom-back of the head (Minecraft pivot
        // (0, 15, -4) → y 9 px, z +4 px); the head box spans z 3..6 px.
        c.head = m.add({"head", -1, {0, 9 * PX, 4 * PX}, {0, 3 * PX, 0.5f * PX}, {2 * PX, 3 * PX, 1.5f * PX}, FEATHER});
        // Beak: 4×2×2 px in front of the head (y 11..13, z 6..8).
        m.add({"beak", c.head, {0, 0, 0}, {0, 3 * PX, 3 * PX}, {2 * PX, 1 * PX, 1 * PX}, BEAK});
        // Wattle: 2×2×2 px hanging under the beak (y 9..11, z 5..7).
        m.add({"wattle", c.head, {0, 0, 0}, {0, 1 * PX, 2 * PX}, {1 * PX, 1 * PX, 1 * PX}, WATTLE});
        // Eyes: 1-px squares at the top corners of the head's front face
        // (z = 6 px), just proud of it so they don't z-fight.
        m.add({"eye_l", c.head, {0, 0, 0}, {-1.5f * PX, 4.5f * PX, 2.05f * PX}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
               EYE});
        m.add({"eye_r", c.head, {0, 0, 0}, {1.5f * PX, 4.5f * PX, 2.05f * PX}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
               EYE});

        // Wings: 1×4×6 px, pivot at the top (shoulder) so a Z rotation
        // swings the tip outward.
        c.leftWing = m.add({"wing_l", -1, {-3 * PX, 11 * PX, 0}, {-0.5f * PX, -2 * PX, 0}, {0.5f * PX, 2 * PX, 3 * PX},
                            WING});
        c.rightWing = m.add({"wing_r", -1, {3 * PX, 11 * PX, 0}, {0.5f * PX, -2 * PX, 0}, {0.5f * PX, 2 * PX, 3 * PX},
                             WING});

        auto leg = [&](const char* name, float x) {
            // Thin 1×5×1 px shank from the hip, 3×0.5×3 px foot at the bottom.
            int idx = m.add({name, -1, {x, 5 * PX, 0.5f * PX}, {0, -2.5f * PX, 0}, {0.5f * PX, 2.5f * PX, 0.5f * PX}, LEG});
            m.add({"foot", idx, {0, -4.75f * PX, 0.5f * PX}, {0, 0, 0}, {1.5f * PX, 0.25f * PX, 1.5f * PX}, LEG});
            return idx;
        };
        c.leftLeg = leg("leg_l", -1.5f * PX);
        c.rightLeg = leg("leg_r", 1.5f * PX);
        return c;
    }();
    return cm;
}

} // namespace

Chicken::Chicken() : Mob(MobType::Chicken) {
    goals.add(0, std::make_unique<PanicGoal>(PANIC_SPEED));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(6.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Chicken::model() const {
    return chickenModel().model;
}

void Chicken::customTick(MobContext& ctx) {
    // Wing flap (Minecraft Chicken.aiStep): amplitude ramps up 1.2/tick in
    // the air and down 0.3/tick on the ground; the phase speed kicks back
    // to 1 whenever airborne and otherwise decays.
    prevFlap = flap;
    prevFlapSpeed = flapSpeed;
    flapSpeed = std::clamp(flapSpeed + (onGround ? -1.0f : 4.0f) * 0.3f, 0.0f, 1.0f);
    if (!onGround && flapping < 1.0f) flapping = 1.0f;
    flapping *= 0.9f;
    flap += flapping * 2.0f;

    // Flutter: damp downward velocity every tick while falling (after the
    // gravity step, as in Minecraft). Terminal speed ≈ 0.11 blocks/tick
    // instead of ~3.9; water keeps the base buoyancy.
    if (!onGround && !inWater && velocity.y < 0.0f) velocity.y *= FALL_SLOWDOWN;

    if (!isAlive()) return;
    if (eggTime < 0) eggTime = ctx.randInt(EGG_MIN_TICKS, EGG_MAX_TICKS);
    if (--eggTime <= 0) {
        layEgg(ctx);
        eggTime = ctx.randInt(EGG_MIN_TICKS, EGG_MAX_TICKS);
    }
}

void Chicken::layEgg(MobContext& ctx) {
    // No item system: drop a small cream, harmless "projectile" just behind
    // the chicken. It falls, sticks where it lands and despawns on its own
    // (Projectile::STUCK_TICKS) — the projectile renderer draws it as a
    // short pale peg.
    ProjectileSpec egg;
    egg.pos = pos - forward() * 0.15f + glm::vec3(0.0f, 0.25f, 0.0f);
    egg.velocity = glm::vec3(0.0f, -0.05f, 0.0f);
    egg.damage = 0.0f;
    egg.gravity = 0.04f;
    egg.drag = 0.98f;
    egg.lifetimeTicks = 200;
    egg.hurtsPlayer = false;
    egg.hurtsMobs = false;
    egg.shooterId = id;
    egg.color = EGG;
    ctx.spawnProjectile(egg);
    ++eggsLaid;
}

float Chicken::wingAngle(float partialTick) const {
    float phase = prevFlap + (flap - prevFlap) * partialTick;
    float amp = prevFlapSpeed + (flapSpeed - prevFlapSpeed) * partialTick;
    return (std::sin(phase) + 1.0f) * amp;
}

MobPose Chicken::pose(float partialTick) const {
    const ChickenModel& cm = chickenModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseBipedLegs(p, cm.leftLeg, cm.rightLeg, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, cm.head, relYaw, renderHeadPitch(partialTick));
    // +Z rotation swings a hanging part's bottom toward +X, so the -X wing
    // flaps outward with a negative angle and the +X wing with a positive one.
    float wing = wingAngle(partialTick);
    p.rotation[cm.leftWing].z = -wing;
    p.rotation[cm.rightWing].z = wing;
    return p;
}
