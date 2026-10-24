#include "mobs/pig.h"

#include <memory>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 PINK(0.94f, 0.64f, 0.64f);
const glm::vec3 SNOUT_PINK(0.90f, 0.52f, 0.56f);
const glm::vec3 NOSTRIL(0.45f, 0.22f, 0.25f);
const glm::vec3 HOOF(0.62f, 0.40f, 0.40f);
const glm::vec3 EYE_WHITE(0.96f, 0.96f, 0.96f);
const glm::vec3 EYE_PUPIL(0.08f, 0.06f, 0.08f);

struct PigModel {
    MobModel model;
    int head, frontLeft, frontRight, backLeft, backRight;
};

// Built once. Dimensions from Minecraft's PigModel converted to +Z-forward,
// feet-origin space: body 10×8×16 px spanning y 6..14, head 8³ px in front,
// 4×6×4 legs hanging from hips at y = 6 px.
const PigModel& pigModel() {
    static const PigModel pm = [] {
        PigModel p;
        MobModel& m = p.model;
        m.add({"body", -1, {0, 10 * PX, 0}, {0, 0, 0}, {5 * PX, 4 * PX, 8 * PX}, PINK});

        p.head = m.add({"head", -1, {0, 12 * PX, 6 * PX}, {0, 0, 4 * PX}, {4 * PX, 4 * PX, 4 * PX}, PINK});
        // Face details sit just proud of the head's front face (z = +8 px
        // from the head pivot) so they don't z-fight.
        int snout = m.add({"snout", p.head, {0, -1.5f * PX, 8.5f * PX}, {0, 0, 0}, {2 * PX, 1.5f * PX, 0.5f * PX},
                           SNOUT_PINK});
        m.add({"nostril_l", snout, {-1 * PX, 0, 0.52f * PX}, {0, 0, 0}, {0.5f * PX, 0.75f * PX, 0.05f * PX}, NOSTRIL});
        m.add({"nostril_r", snout, {1 * PX, 0, 0.52f * PX}, {0, 0, 0}, {0.5f * PX, 0.75f * PX, 0.05f * PX}, NOSTRIL});
        m.add({"eye_l", p.head, {-3 * PX, 1 * PX, 8.05f * PX}, {0, 0, 0}, {1 * PX, 0.5f * PX, 0.05f * PX}, EYE_WHITE});
        m.add({"eye_r", p.head, {3 * PX, 1 * PX, 8.05f * PX}, {0, 0, 0}, {1 * PX, 0.5f * PX, 0.05f * PX}, EYE_WHITE});
        m.add({"pupil_l", p.head, {-2.5f * PX, 1 * PX, 8.1f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
               EYE_PUPIL});
        m.add({"pupil_r", p.head, {2.5f * PX, 1 * PX, 8.1f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
               EYE_PUPIL});

        auto leg = [&](const char* name, float x, float z) {
            int idx = m.add({name, -1, {x, 6 * PX, z}, {0, -3 * PX, 0}, {2 * PX, 3 * PX, 2 * PX}, PINK});
            // Hoof: darker band at the bottom of the leg, slightly wider.
            m.add({"hoof", idx, {0, -5.5f * PX, 0}, {0, 0, 0}, {2.05f * PX, 0.55f * PX, 2.05f * PX}, HOOF});
            return idx;
        };
        p.frontLeft = leg("leg_fl", -3 * PX, 5 * PX);
        p.frontRight = leg("leg_fr", 3 * PX, 5 * PX);
        p.backLeft = leg("leg_bl", -3 * PX, -7 * PX);
        p.backRight = leg("leg_br", 3 * PX, -7 * PX);
        return p;
    }();
    return pm;
}

} // namespace

Pig::Pig() : Mob(MobType::Pig) {
    goals.add(0, std::make_unique<PanicGoal>(PANIC_SPEED));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(6.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Pig::model() const {
    return pigModel().model;
}

MobPose Pig::pose(float partialTick) const {
    const PigModel& pm = pigModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseQuadrupedLegs(p, pm.frontLeft, pm.frontRight, pm.backLeft, pm.backRight, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, pm.head, relYaw, renderHeadPitch(partialTick));
    return p;
}
