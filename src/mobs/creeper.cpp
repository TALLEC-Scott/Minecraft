#include "mobs/creeper.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel
constexpr float FOLLOW_RANGE = 16.0f;

const glm::vec3 GREEN(0.36f, 0.68f, 0.30f);
const glm::vec3 GREEN_DARK(0.25f, 0.52f, 0.21f);
const glm::vec3 GREEN_LIGHT(0.50f, 0.80f, 0.42f);
const glm::vec3 GREEN_PALE(0.66f, 0.84f, 0.60f);
const glm::vec3 FACE_BLACK(0.05f, 0.06f, 0.05f);

struct CreeperModel {
    MobModel model;
    int head, frontLeft, frontRight, backLeft, backRight;
};

// Built once. Minecraft's CreeperModel in +Z-forward, feet-origin space:
// 4×6×4 legs (y 0..6 px) sticking out front and back of an 8×12×4 body
// (y 6..18), 8³ head on top (y 18..26). Thin boxes 0.1 px proud of the
// faces give the mottled texture and the black face.
const CreeperModel& creeperModel() {
    static const CreeperModel cm = [] {
        CreeperModel c;
        MobModel& m = c.model;
        int body = m.add({"body", -1, {0, 12 * PX, 0}, {0, 0, 0}, {4 * PX, 6 * PX, 2 * PX}, GREEN});
        // Mottling on the body (front z = +2 px, back -2, sides x = ±4).
        auto patch = [&](int parent, glm::vec3 at, glm::vec3 half, glm::vec3 color) {
            m.add({"patch", parent, at * PX, {0, 0, 0}, half * PX, color});
        };
        patch(body, {-2, 3, 2.05f}, {1, 1, 0.05f}, GREEN_DARK);
        patch(body, {2, -1, 2.05f}, {1, 1.5f, 0.05f}, GREEN_LIGHT);
        patch(body, {-1, -4, 2.05f}, {1.5f, 1, 0.05f}, GREEN_DARK);
        patch(body, {1, 2, -2.05f}, {1.5f, 1, 0.05f}, GREEN_DARK);
        patch(body, {-2, -3, -2.05f}, {1, 1.5f, 0.05f}, GREEN_PALE);
        patch(body, {4.05f, 1, 0}, {0.05f, 1.5f, 1}, GREEN_DARK);
        patch(body, {-4.05f, -2, 0}, {0.05f, 1, 1}, GREEN_LIGHT);

        c.head = m.add({"head", -1, {0, 18 * PX, 0}, {0, 4 * PX, 0}, {4 * PX, 4 * PX, 4 * PX}, GREEN});
        patch(c.head, {-1, 8.05f, 1}, {1.5f, 0.05f, 1.5f}, GREEN_DARK);
        patch(c.head, {2, 8.05f, -2}, {1, 0.05f, 1}, GREEN_LIGHT);
        patch(c.head, {4.05f, 5, -1}, {0.05f, 1, 1.5f}, GREEN_DARK);
        patch(c.head, {-4.05f, 3, 1}, {0.05f, 1.5f, 1}, GREEN_PALE);
        patch(c.head, {1, 4, -4.05f}, {1.5f, 1, 0.05f}, GREEN_DARK);
        patch(c.head, {-2.5f, 7, 4.05f}, {1, 0.5f, 0.05f}, GREEN_LIGHT);

        // Face on the 8×8 front (z = +4 px), head pivot at its bottom so
        // texture row r (0 = top) sits at y = 7.5 - r px:
        //   ........   ← rows 0-1 blank
        //   .XX..XX.   ← eyes, rows 2-3
        //   ...XX...   ← nose, row 4
        //   ..XXXX..   ← mouth, rows 5-6
        //   ..X..X..   ← mouth corners, row 7
        auto face = [&](const char* name, float x, float y, float hx, float hy) {
            m.add({name, c.head, {x * PX, y * PX, 4.05f * PX}, {0, 0, 0}, {hx * PX, hy * PX, 0.05f * PX}, FACE_BLACK});
        };
        face("eye_l", -2, 5, 1, 1);
        face("eye_r", 2, 5, 1, 1);
        face("nose", 0, 3.5f, 1, 0.5f);
        face("mouth", 0, 2, 2, 1);
        face("mouth_l", -1.5f, 0.5f, 0.5f, 0.5f);
        face("mouth_r", 1.5f, 0.5f, 0.5f, 0.5f);

        auto leg = [&](const char* name, float x, float z) {
            return m.add({name, -1, {x * PX, 6 * PX, z * PX}, {0, -3 * PX, 0}, {2 * PX, 3 * PX, 2 * PX}, GREEN});
        };
        c.frontLeft = leg("leg_fl", -2, 4);
        c.frontRight = leg("leg_fr", 2, 4);
        c.backLeft = leg("leg_bl", -2, -4);
        c.backRight = leg("leg_br", 2, -4);
        return c;
    }();
    return cm;
}

// Walk straight at the player (no cliff avoidance) — Minecraft's
// MeleeAttackGoal minus the hit; the swell goal takes over up close.
class CreeperChaseGoal : public MobGoal {
  public:
    CreeperChaseGoal() : MobGoal(GOAL_MOVE | GOAL_LOOK) {}
    bool canStart(Mob& mob, MobContext& ctx) override {
        return ctx.playerTargetable && glm::distance(mob.pos, ctx.playerFeet) < FOLLOW_RANGE;
    }
    bool shouldContinue(Mob& mob, MobContext& ctx) override {
        return ctx.playerTargetable && glm::distance(mob.pos, ctx.playerFeet) < FOLLOW_RANGE * 1.5f;
    }
    void stop(Mob& mob, MobContext& /*ctx*/) override { mob.stopNavigation(); }
    void tick(Mob& mob, MobContext& ctx) override {
        mob.lookAt(ctx.playerEye, 30.0f, 30.0f);
        if (glm::distance(mob.pos, ctx.playerFeet) > 1.5f)
            mob.navigateTo(ctx.playerFeet, Creeper::CHASE_SPEED, false);
        else
            mob.stopNavigation();
    }
};

// Minecraft SwellGoal: starts when the player is within 3 blocks (or the
// fuse is already burning), holds still and stares while swelling, and
// flips the fuse to deflate once the player is out of sight or > 7 away.
class SwellGoal : public MobGoal {
  public:
    explicit SwellGoal(Creeper& c) : MobGoal(GOAL_MOVE | GOAL_LOOK), creeper(c) {}
    bool canStart(Mob& mob, MobContext& ctx) override {
        if (swelling) return true;
        return ctx.playerTargetable && glm::distance(mob.pos, ctx.playerFeet) < Creeper::IGNITE_RANGE;
    }
    void start(Mob& mob, MobContext& /*ctx*/) override { mob.stopNavigation(); }
    void stop(Mob& /*mob*/, MobContext& /*ctx*/) override {
        swelling = false;
        creeper.setSwellDir(-1);
    }
    void tick(Mob& mob, MobContext& ctx) override {
        mob.stopNavigation();
        swelling = ctx.playerTargetable && glm::distance(mob.pos, ctx.playerFeet) <= Creeper::DEFUSE_RANGE &&
                   mobai::hasLineOfSight(ctx, mob.eyePos(), ctx.playerEye);
        if (ctx.playerTargetable) mob.lookAt(ctx.playerEye, 30.0f, 30.0f);
        creeper.setSwellDir(swelling ? 1 : -1);
    }

  private:
    Creeper& creeper;
    bool swelling = false;
};

} // namespace

Creeper::Creeper() : Mob(MobType::Creeper) {
    goals.add(1, std::make_unique<SwellGoal>(*this));
    goals.add(2, std::make_unique<CreeperChaseGoal>());
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(8.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Creeper::model() const {
    return creeperModel().model;
}

void Creeper::customTick(MobContext& ctx) {
    prevSwell = swell;
    if (!isAlive()) return;
    if (swellDir > 0 && swell == 0) queueSound(MobSoundEvent::Attack); // hiss
    swell = std::clamp(swell + swellDir, 0, FUSE_TICKS);
    if (swell >= FUSE_TICKS) {
        // Mark removed BEFORE exploding: MobManager::applyExplosion skips
        // non-alive mobs, so the creeper can't damage (or kill-animate)
        // itself. Safe mid-tick — applyExplosion only iterates the mob list
        // and MobManager erases removed mobs after the tick loop.
        removed = true;
        stopNavigation();
        ctx.explode(center(), EXPLOSION_POWER);
    }
}

float Creeper::swelling(float partialTick) const {
    float s = static_cast<float>(prevSwell) + static_cast<float>(swell - prevSwell) * partialTick;
    return s / static_cast<float>(FUSE_TICKS - 2);
}

MobPose Creeper::pose(float partialTick) const {
    const CreeperModel& cm = creeperModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseQuadrupedLegs(p, cm.frontLeft, cm.frontRight, cm.backLeft, cm.backRight, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, cm.head, relYaw, renderHeadPitch(partialTick));

    // Minecraft CreeperRenderer.scale + getWhiteOverlayProgress: a fast
    // wobble, then an ease-in puff (+40 % wide, +10 % tall) and a white
    // flash that blinks on alternate tenths of the fuse.
    float f = swelling(partialTick);
    if (f > 0.0f) {
        float wobble = 1.0f + std::sin(f * 100.0f) * f * 0.01f;
        float e = std::clamp(f, 0.0f, 1.0f);
        e *= e;
        e *= e;
        float horiz = (1.0f + e * 0.4f) * wobble;
        float vert = (1.0f + e * 0.1f) / wobble;
        p.scale = glm::vec3(horiz, vert, horiz);
        int blink = static_cast<int>(f * 10.0f);
        p.whiteFlash = (blink % 2 == 0) ? 0.0f : std::clamp(f, 0.5f, 1.0f);
    }
    return p;
}
