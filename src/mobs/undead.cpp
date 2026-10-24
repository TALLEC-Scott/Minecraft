#include "mobs/undead.h"

#include <algorithm>
#include <cmath>

namespace {

const glm::vec3 FLAME_ORANGE(1.0f, 0.50f, 0.08f);
const glm::vec3 FLAME_YELLOW(1.0f, 0.82f, 0.22f);

// Flame anchors around the body (feet-origin blocks), spread over the
// front/back/sides from shins to head so the whole mob reads as burning.
// The 4th component is a phase offset so the licks don't rise in sync.
constexpr float FLAME_ANCHORS[UndeadMob::FLAME_COUNT][4] = {
    {-0.28f, 0.30f, 0.16f, 0.00f}, {0.26f, 0.55f, -0.18f, 0.35f}, {0.06f, 0.95f, 0.20f, 0.70f},
    {-0.24f, 1.20f, -0.16f, 0.15f}, {0.27f, 1.45f, 0.12f, 0.55f}, {-0.05f, 1.85f, -0.10f, 0.85f},
};

constexpr float FLAME_RISE = 0.25f;  // blocks a lick climbs before it resets
constexpr float FLAME_RATE = 0.12f;  // cycles per tick (~2.4 / s)

} // namespace

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
    if (inWater) {
        fireTicks = 0;
    } else if (!dying && sunExposed(ctx)) {
        fireTicks = std::max(fireTicks, SUN_FIRE_TICKS);
    }

    if (fireTicks <= 0) {
        burnTimer = 0;
        return;
    }
    // First damage the tick we ignite, then once per BURN_INTERVAL. No
    // knockback; i-frames from a recent player hit can swallow one tick
    // of fire damage, as in Minecraft.
    if (burnTimer <= 0) {
        damage(BURN_DAMAGE, pos, 0.0f);
        burnTimer = BURN_INTERVAL;
    }
    --burnTimer;
    --fireTicks;
}

UndeadMob::FlameParts UndeadMob::addFlameParts(MobModel& m) {
    FlameParts idx{};
    for (int i = 0; i < FLAME_COUNT; ++i) {
        const float* a = FLAME_ANCHORS[i];
        idx[i] = m.add({"flame", -1, {a[0], a[1], a[2]}, {0, 0, 0}, {0.09f, 0.14f, 0.09f},
                        (i % 2 == 0) ? FLAME_ORANGE : FLAME_YELLOW});
        m.parts[idx[i]].emissive = true;
    }
    return idx;
}

void UndeadMob::poseFlames(MobPose& pose, const FlameParts& flames, float partialTick) const {
    bool on = burning();
    float t = static_cast<float>(ageTicks) + partialTick;
    for (int i = 0; i < FLAME_COUNT; ++i) {
        int p = flames[i];
        if (p < 0 || p >= MAX_MOB_PARTS) continue;
        pose.hidden[p] = !on;
        if (!on) continue;
        float cycle = t * FLAME_RATE + FLAME_ANCHORS[i][3];
        float phase = cycle - std::floor(cycle); // 0..1 rise, then reset
        pose.offset[p].y += phase * FLAME_RISE;
        pose.rotation[p].y += cycle * 2.0f; // slow twist so the box reads as a flicker
    }
}
