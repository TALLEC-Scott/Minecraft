#include "mobs/undead.h"

bool UndeadMob::sunExposed(const MobContext& ctx) const {
    if (!ctx.isDay || inWater) return false;
    glm::ivec3 eye = MobContext::blockAtPos(eyePos());
    if (eye.y >= CHUNK_HEIGHT) return true;
    if (eye.y < 0) return false;
    // Stored sky light is 15 only where the column is open to the sky, so
    // any roof / overhang / tree canopy above counts as shade.
    return ctx.blocks->skyLightAt(eye.x, eye.y, eye.z) >= 15;
}

void UndeadMob::customTick(MobContext& ctx) {
    if (!dying && sunExposed(ctx)) setOnFire(SUN_FIRE_TICKS);
}
