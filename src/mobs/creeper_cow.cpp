#include "mobs/creeper_cow.h"

namespace {

// Creeper greens on the cow geometry; the black face bits stay dark.
const CowPalette CREEPER_COW_PALETTE{
    {0.36f, 0.68f, 0.30f}, // hide
    {0.25f, 0.52f, 0.21f}, // patch
    {0.50f, 0.80f, 0.42f}, // muzzle
    {0.05f, 0.06f, 0.05f}, // nostril
    {0.66f, 0.84f, 0.60f}, // horn
    {0.25f, 0.52f, 0.21f}, // udder
    {0.20f, 0.40f, 0.17f}, // hoof
    {0.05f, 0.06f, 0.05f}, // eye white → creeper-black eyes
    {0.05f, 0.06f, 0.05f}, // pupil
};

} // namespace

CreeperCow::CreeperCow() : Cow(MobType::CreeperCow) {}

const MobModel& CreeperCow::model() const {
    static const CowModelParts cm = buildCowModel(CREEPER_COW_PALETTE);
    return cm.model;
}

MobPose CreeperCow::pose(float partialTick) const {
    MobPose p = Cow::pose(partialTick);
    // Lit-fuse blink while falling, like a creeper about to go off.
    if (airTicks > 0 && (ageTicks / 3) % 2 == 1) p.whiteFlash = 0.7f;
    return p;
}

void CreeperCow::customTick(MobContext& ctx) {
    if (removed) return;
    if (!onGround && !inWater) {
        ++airTicks;
        return;
    }
    if (airTicks >= MIN_FALL_TICKS) {
        // Same order as Creeper::customTick: removed first so the blast
        // (MobManager::applyExplosion skips non-alive mobs) can't hit us.
        removed = true;
        stopNavigation();
        ctx.explode(center(), EXPLOSION_POWER);
    }
    airTicks = 0;
}
