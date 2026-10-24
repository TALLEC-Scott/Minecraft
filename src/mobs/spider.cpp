#include "mobs/spider.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <glm/gtc/constants.hpp>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 BODY_GRAY(0.24f, 0.21f, 0.20f);
const glm::vec3 BODY_DARK(0.16f, 0.14f, 0.13f);
const glm::vec3 BODY_MARK(0.32f, 0.28f, 0.26f);
const glm::vec3 LEG_GRAY(0.20f, 0.18f, 0.17f);
const glm::vec3 EYE_RED(0.90f, 0.10f, 0.08f);
const glm::vec3 EYE_RED_SMALL(0.72f, 0.06f, 0.05f);

// Leg pairs front → back. Spread is the yaw of a leg off the sideways
// axis (+ = tip swings toward the head); droop tilts it down toward the
// ground. Minecraft SpiderModel: ±45° / ±22.5° spread, 45° droop on the
// outer pairs, 0.74 × that on the middle ones.
constexpr int LEG_PAIRS = 4;
constexpr float QUARTER = glm::pi<float>() / 4.0f;
constexpr float LEG_SPREAD[LEG_PAIRS] = {QUARTER, QUARTER * 0.5f, -QUARTER * 0.5f, -QUARTER};
constexpr float LEG_DROOP[LEG_PAIRS] = {QUARTER, QUARTER * 0.74f, QUARTER * 0.74f, QUARTER};
constexpr float LEG_Z[LEG_PAIRS] = {1.0f, 0.0f, -1.0f, -2.0f}; // hip z (px), all on the thorax
// Gait phase per pair (Minecraft: front 1.5π, middle-front π/2, middle-hind π, hind 0).
constexpr float LEG_PHASE[LEG_PAIRS] = {glm::pi<float>() * 1.5f, glm::pi<float>() * 0.5f, glm::pi<float>(), 0.0f};

struct SpiderModel {
    MobModel model;
    int head;
    int legLeft[LEG_PAIRS], legRight[LEG_PAIRS]; // left = +X side, right = -X
};

// Built once. Minecraft's SpiderModel in +Z-forward, feet-origin space:
// 6³ thorax centered 9 px up, 8³ head in front of it, 10×8×12 abdomen
// behind, and eight 16×2×2 legs hinged at the thorax sides.
const SpiderModel& spiderModel() {
    static const SpiderModel sm = [] {
        SpiderModel s;
        MobModel& m = s.model;
        int thorax = m.add({"thorax", -1, {0, 9 * PX, 0}, {0, 0, 0}, {3 * PX, 3 * PX, 3 * PX}, BODY_DARK});
        int abdomen = m.add({"abdomen", -1, {0, 9 * PX, -9 * PX}, {0, 0, 0}, {5 * PX, 4 * PX, 6 * PX}, BODY_GRAY});
        // Lighter markings along the top of the abdomen (top face y = +4 px).
        m.add({"mark", abdomen, {0, 4.05f * PX, 2 * PX}, {0, 0, 0}, {1.5f * PX, 0.05f * PX, 2 * PX}, BODY_MARK});
        m.add({"mark", abdomen, {0, 4.05f * PX, -3 * PX}, {0, 0, 0}, {2.5f * PX, 0.05f * PX, 1 * PX}, BODY_MARK});

        s.head = m.add({"head", -1, {0, 9 * PX, 3 * PX}, {0, 0, 4 * PX}, {4 * PX, 4 * PX, 4 * PX}, BODY_GRAY});
        // Eight eyes on the head's front face (z = +8 px from the pivot):
        // two big 2×2 ones in the middle, six 1×1 around them.
        auto eye = [&](float x, float y, float half, glm::vec3 color) {
            m.add({"eye", s.head, {x * PX, y * PX, 8.05f * PX}, {0, 0, 0}, {half * PX, half * PX, 0.05f * PX}, color});
        };
        eye(-1.5f, 0.0f, 1.0f, EYE_RED);
        eye(1.5f, 0.0f, 1.0f, EYE_RED);
        eye(-0.5f, 2.0f, 0.5f, EYE_RED_SMALL);
        eye(0.5f, 2.0f, 0.5f, EYE_RED_SMALL);
        eye(-3.0f, 1.5f, 0.5f, EYE_RED_SMALL);
        eye(3.0f, 1.5f, 0.5f, EYE_RED_SMALL);
        eye(-3.0f, -1.0f, 0.5f, EYE_RED_SMALL);
        eye(3.0f, -1.0f, 0.5f, EYE_RED_SMALL);

        // Legs: hip at the thorax side (x = ±4 px, child of the thorax so
        // pivots are relative to its center), box running 15 px
        // outward. Z·Y·X order: the spread (Y) is applied before the droop
        // (Z). A +X leg's tip swings toward +Z under a NEGATIVE Y rotation
        // and dips under a negative Z rotation; the -X side mirrors both.
        for (int i = 0; i < LEG_PAIRS; ++i) {
            MobPart left{"leg_l", thorax, {4 * PX, 0, LEG_Z[i] * PX}, {7 * PX, 0, 0}, {8 * PX, 1 * PX, 1 * PX},
                         LEG_GRAY};
            left.restRotation = {0, -LEG_SPREAD[i], -LEG_DROOP[i]};
            s.legLeft[i] = m.add(left);
            MobPart right = left;
            right.name = "leg_r";
            right.pivot.x = -4 * PX;
            right.center.x = -7 * PX;
            right.restRotation = {0, LEG_SPREAD[i], LEG_DROOP[i]};
            s.legRight[i] = m.add(right);
        }
        return s;
    }();
    return sm;
}

// Minecraft SpiderAttackGoal: melee chase that only begins in the dark
// (or when provoked) and, once going, may give up 1 % of ticks in light.
class SpiderAttackGoal : public ChaseAndMeleeGoal {
  public:
    explicit SpiderAttackGoal(Spider& s)
        : ChaseAndMeleeGoal(Spider::CHASE_SPEED, Spider::ATTACK_DAMAGE, 16.0f, Spider::ATTACK_COOLDOWN), spider(s) {}
    bool canStart(Mob& mob, MobContext& ctx) override {
        if (!spider.isProvoked() && !spider.inDarkness(ctx)) return false;
        return ChaseAndMeleeGoal::canStart(mob, ctx);
    }
    bool shouldContinue(Mob& mob, MobContext& ctx) override {
        if (!ChaseAndMeleeGoal::shouldContinue(mob, ctx)) return false;
        if (spider.isProvoked() || spider.inDarkness(ctx)) return true;
        return !ctx.chance(0.01f);
    }

  private:
    Spider& spider;
};

} // namespace

Spider::Spider() : Mob(MobType::Spider) {
    goals.add(4, std::make_unique<SpiderAttackGoal>(*this));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(8.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Spider::model() const {
    return spiderModel().model;
}

bool Spider::inDarkness(const MobContext& ctx) const {
    glm::ivec3 b = MobContext::blockAtPos(eyePos());
    return ctx.lightAt(b.x, b.y, b.z) <= MAX_HOSTILE_LIGHT;
}

void Spider::onHurt(float /*amount*/, glm::vec3 /*sourcePos*/) {
    provokedTicks = PROVOKED_TICKS;
}

void Spider::aiTick(MobContext& ctx) {
    Mob::aiTick(ctx);
    if (provokedTicks > 0) --provokedTicks;
    attacking = goals.isRunning<SpiderAttackGoal>();

    // Minecraft LeapAtTargetGoal: pounce from mid range while grounded.
    if (attacking && onGround && !inWater) {
        float d = glm::distance(pos, ctx.playerFeet);
        if (d >= LEAP_MIN_DIST && d <= LEAP_MAX_DIST && ctx.chance(LEAP_CHANCE)) leapToward(ctx.playerFeet);
    }
}

void Spider::leapToward(glm::vec3 target) {
    glm::vec2 d(target.x - pos.x, target.z - pos.z);
    float len = glm::length(d);
    if (len > 1e-4f) d /= len;
    velocity.x = d.x * LEAP_HORIZONTAL + velocity.x * 0.2f;
    velocity.z = d.y * LEAP_HORIZONTAL + velocity.z * 0.2f;
    velocity.y = LEAP_UP;
    ++leaps;
}

MobPose Spider::pose(float partialTick) const {
    const SpiderModel& sm = spiderModel();
    MobPose p;
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, sm.head, relYaw, renderHeadPitch(partialTick));

    // Minecraft SpiderModel.setupAnim: each pair sweeps fore/aft (Y) at
    // double limb frequency and lifts (Z) on its own phase, so the gait
    // ripples front to back; left and right mirror.
    glm::vec2 anim = walkAnim(partialTick);
    constexpr float FREQ = 0.6662f;
    for (int i = 0; i < LEG_PAIRS; ++i) {
        float sweep = -std::cos(anim.x * FREQ * 2.0f + LEG_PHASE[i]) * 0.4f * anim.y;
        float lift = std::abs(std::sin(anim.x * FREQ + LEG_PHASE[i]) * 0.4f) * anim.y;
        p.rotation[sm.legRight[i]].y -= sweep;
        p.rotation[sm.legLeft[i]].y += sweep;
        p.rotation[sm.legRight[i]].z -= lift;
        p.rotation[sm.legLeft[i]].z += lift;
    }
    return p;
}
