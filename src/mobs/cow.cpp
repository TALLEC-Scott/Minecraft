#include "mobs/cow.h"

#include <memory>

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 HIDE_DARK(0.16f, 0.12f, 0.10f);
const glm::vec3 HIDE_WHITE(0.92f, 0.91f, 0.88f);
const glm::vec3 MUZZLE(0.80f, 0.70f, 0.66f);
const glm::vec3 NOSTRIL(0.30f, 0.20f, 0.20f);
const glm::vec3 HORN(0.86f, 0.84f, 0.76f);
const glm::vec3 UDDER_PINK(0.93f, 0.62f, 0.66f);
const glm::vec3 HOOF(0.28f, 0.25f, 0.23f);
const glm::vec3 EYE_WHITE(0.96f, 0.96f, 0.96f);
const glm::vec3 EYE_PUPIL(0.08f, 0.06f, 0.08f);

const CowPalette COW_PALETTE{HIDE_DARK, HIDE_WHITE, MUZZLE, NOSTRIL, HORN, UDDER_PINK, HOOF, EYE_WHITE, EYE_PUPIL};

const CowModelParts& cowModel() {
    static const CowModelParts cm = buildCowModel(COW_PALETTE);
    return cm;
}

} // namespace

// Dimensions from Minecraft's CowModel converted to +Z-forward,
// feet-origin space: body 12×10×18 px spanning y 12..22 / z -10..8, head
// 8×8×6 px in front of it, 4×12×4 legs hanging from hips at y = 12 px.
CowModelParts buildCowModel(const CowPalette& pal) {
    CowModelParts c;
    MobModel& m = c.model;
    int body = m.add({"body", -1, {0, 17 * PX, -1 * PX}, {0, 0, 0}, {6 * PX, 5 * PX, 9 * PX}, pal.hide});
    // White patches: boxes ~0.1 px proud of the body that each wrap two
    // faces (side + top/back/belly/chest), so the hide reads as patchy
    // from every angle. Positions are relative to the body center.
    m.add({"spot_back_l",
           body,
           {-4.05f * PX, 2.55f * PX, -2.5f * PX},
           {0, 0, 0},
           {2.05f * PX, 2.55f * PX, 3.5f * PX},
           pal.patch});
    m.add({"spot_rump_r", body, {4.55f * PX, 0, -6.05f * PX}, {0, 0, 0}, {1.55f * PX, 3 * PX, 3.05f * PX}, pal.patch});
    m.add({"spot_flank_r",
           body,
           {4.05f * PX, 2.05f * PX, 4 * PX},
           {0, 0, 0},
           {2.05f * PX, 3.05f * PX, 2 * PX},
           pal.patch});
    m.add({"spot_belly", body, {0, -4.05f * PX, 3.5f * PX}, {0, 0, 0}, {4 * PX, 1.05f * PX, 3.5f * PX}, pal.patch});
    m.add({"spot_chest_l",
           body,
           {-4.55f * PX, -2 * PX, 6.55f * PX},
           {0, 0, 0},
           {1.55f * PX, 2 * PX, 2.55f * PX},
           pal.patch});
    // Udder: 4×1×6 px under the rear belly (y 11..12, z -10..-4).
    m.add({"udder", body, {0, -5.5f * PX, -6 * PX}, {0, 0, 0}, {2 * PX, 0.5f * PX, 3 * PX}, pal.udder});

    c.head = m.add({"head", -1, {0, 20 * PX, 8 * PX}, {0, 0, 3 * PX}, {4 * PX, 4 * PX, 3 * PX}, pal.hide});
    // Head box spans x ±4, y ±4, z 0..6 px from the pivot. White blaze
    // wraps the top-right-front corner.
    m.add({"head_spot",
           c.head,
           {1.05f * PX, 2.55f * PX, 4.05f * PX},
           {0, 0, 0},
           {3.05f * PX, 1.55f * PX, 2.05f * PX},
           pal.patch});
    m.add(
        {"horn_l", c.head, {-4.5f * PX, 3.5f * PX, 3.5f * PX}, {0, 0, 0}, {0.5f * PX, 1.5f * PX, 0.5f * PX}, pal.horn});
    m.add(
        {"horn_r", c.head, {4.5f * PX, 3.5f * PX, 3.5f * PX}, {0, 0, 0}, {0.5f * PX, 1.5f * PX, 0.5f * PX}, pal.horn});
    // Muzzle protrudes 1 px from the lower face (z 6..7, y -4..-1).
    int muzzle =
        m.add({"muzzle", c.head, {0, -2.5f * PX, 6.5f * PX}, {0, 0, 0}, {3 * PX, 1.5f * PX, 0.5f * PX}, pal.muzzle});
    m.add({"nostril_l",
           muzzle,
           {-1.5f * PX, 0.25f * PX, 0.52f * PX},
           {0, 0, 0},
           {0.5f * PX, 0.5f * PX, 0.05f * PX},
           pal.nostril});
    m.add({"nostril_r",
           muzzle,
           {1.5f * PX, 0.25f * PX, 0.52f * PX},
           {0, 0, 0},
           {0.5f * PX, 0.5f * PX, 0.05f * PX},
           pal.nostril});
    m.add(
        {"eye_l", c.head, {-3 * PX, 0.5f * PX, 6.05f * PX}, {0, 0, 0}, {1 * PX, 0.5f * PX, 0.05f * PX}, pal.eyeWhite});
    m.add({"eye_r", c.head, {3 * PX, 0.5f * PX, 6.05f * PX}, {0, 0, 0}, {1 * PX, 0.5f * PX, 0.05f * PX}, pal.eyeWhite});
    m.add({"pupil_l",
           c.head,
           {-2.5f * PX, 0.5f * PX, 6.1f * PX},
           {0, 0, 0},
           {0.5f * PX, 0.5f * PX, 0.05f * PX},
           pal.pupil});
    m.add({"pupil_r",
           c.head,
           {2.5f * PX, 0.5f * PX, 6.1f * PX},
           {0, 0, 0},
           {0.5f * PX, 0.5f * PX, 0.05f * PX},
           pal.pupil});

    auto leg = [&](const char* name, float x, float z, bool sock) {
        int idx = m.add({name, -1, {x, 12 * PX, z}, {0, -6 * PX, 0}, {2 * PX, 6 * PX, 2 * PX}, pal.hide});
        // Optional white sock above the hoof (y -10.5..-6.5 from the hip).
        if (sock) m.add({"sock", idx, {0, -8.5f * PX, 0}, {0, 0, 0}, {2.05f * PX, 2 * PX, 2.05f * PX}, pal.patch});
        m.add({"hoof", idx, {0, -11.5f * PX, 0}, {0, 0, 0}, {2.05f * PX, 0.55f * PX, 2.05f * PX}, pal.hoof});
        return idx;
    };
    c.frontLeft = leg("leg_fl", -4 * PX, 6 * PX, true);
    c.frontRight = leg("leg_fr", 4 * PX, 6 * PX, false);
    c.backLeft = leg("leg_bl", -4 * PX, -7 * PX, false);
    c.backRight = leg("leg_br", 4 * PX, -7 * PX, true);
    return c;
}

Cow::Cow() : Cow(MobType::Cow) {}

Cow::Cow(MobType variant) : Mob(variant) {
    goals.add(0, std::make_unique<PanicGoal>(PANIC_SPEED));
    goals.add(5, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(6, std::make_unique<LookAtPlayerGoal>(6.0f));
    goals.add(7, std::make_unique<RandomLookGoal>());
}

const MobModel& Cow::model() const {
    return cowModel().model;
}

MobPose Cow::pose(float partialTick) const {
    const CowModelParts& cm = cowModel();
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseQuadrupedLegs(p, cm.frontLeft, cm.frontRight, cm.backLeft, cm.backRight, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    poseHeadLook(p, cm.head, relYaw, renderHeadPitch(partialTick));
    return p;
}
