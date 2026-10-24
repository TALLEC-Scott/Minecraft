#include "mobs/sheep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include <glm/gtc/constants.hpp>

#include "world.h"

namespace {

constexpr float PX = 1.0f / 16.0f; // one Minecraft model pixel

const glm::vec3 SKIN(0.90f, 0.80f, 0.72f);  // face + lower legs
const glm::vec3 SHORN(0.88f, 0.77f, 0.73f); // body under the fleece
const glm::vec3 NOSE(0.88f, 0.60f, 0.62f);
const glm::vec3 EYE_WHITE(0.96f, 0.96f, 0.96f);
const glm::vec3 EYE_PUPIL(0.08f, 0.06f, 0.08f);

struct SheepModel {
    MobModel model;
    int head, frontLeft, frontRight, backLeft, backRight;
    std::array<int, 6> wool; // body, head, 4 legs — hidden when sheared
};

// Dimensions from Minecraft's SheepModel (skin) + SheepFurModel (fleece,
// inflated boxes) converted to +Z-forward, feet-origin space: body 8×6×16
// px spanning y 12..18, head 6×6×8 px in front, 4×12×4 legs from hips at
// y = 12 px. Fleece: body +1.75 px, head cap +0.6 px, upper legs +0.5 px.
SheepModel buildSheepModel(glm::vec3 wool) {
    SheepModel s;
    MobModel& m = s.model;
    int body = m.add({"body", -1, {0, 15 * PX, 0}, {0, 0, 0}, {4 * PX, 3 * PX, 8 * PX}, SHORN});
    s.wool[0] = m.add({"body_wool", body, {0, 0, 0}, {0, 0, 0}, {5.75f * PX, 4.75f * PX, 9.75f * PX}, wool});

    // Head box spans x ±3, y -2..4, z -2..6 px from the pivot; the fleece
    // cap ends at z = 4.6 so the face pokes out in front of it.
    s.head = m.add({"head", -1, {0, 18 * PX, 8 * PX}, {0, 1 * PX, 2 * PX}, {3 * PX, 3 * PX, 4 * PX}, SKIN});
    s.wool[1] = m.add({"head_wool", s.head, {0, 1 * PX, 1 * PX}, {0, 0, 0}, {3.6f * PX, 3.6f * PX, 3.6f * PX}, wool});
    m.add({"eye_l", s.head, {-2.5f * PX, 1.5f * PX, 6.05f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
           EYE_WHITE});
    m.add({"eye_r", s.head, {2.5f * PX, 1.5f * PX, 6.05f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
           EYE_WHITE});
    m.add({"pupil_l", s.head, {-1.5f * PX, 1.5f * PX, 6.05f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
           EYE_PUPIL});
    m.add({"pupil_r", s.head, {1.5f * PX, 1.5f * PX, 6.05f * PX}, {0, 0, 0}, {0.5f * PX, 0.5f * PX, 0.05f * PX},
           EYE_PUPIL});
    m.add({"nose", s.head, {0, -0.5f * PX, 6.05f * PX}, {0, 0, 0}, {1 * PX, 0.5f * PX, 0.05f * PX}, NOSE});

    int woolSlot = 2;
    auto leg = [&](const char* name, float x, float z) {
        int idx = m.add({name, -1, {x, 12 * PX, z}, {0, -6 * PX, 0}, {2 * PX, 6 * PX, 2 * PX}, SKIN});
        // Woolly upper leg: 5×7×5 px from just above the hip down to y -6.5.
        s.wool[woolSlot++] =
            m.add({"leg_wool", idx, {0, -3 * PX, 0}, {0, 0, 0}, {2.5f * PX, 3.5f * PX, 2.5f * PX}, wool});
        return idx;
    };
    s.frontLeft = leg("leg_fl", -3 * PX, 5 * PX);
    s.frontRight = leg("leg_fr", 3 * PX, 5 * PX);
    s.backLeft = leg("leg_bl", -3 * PX, -7 * PX);
    s.backRight = leg("leg_br", 3 * PX, -7 * PX);
    return s;
}

// One model per fleece color (the renderer tints per part, not per mob);
// part indices are identical across them.
const SheepModel& sheepModel(WoolColor c) {
    static const std::array<SheepModel, WOOL_COLOR_COUNT> models = [] {
        std::array<SheepModel, WOOL_COLOR_COUNT> a;
        for (int i = 0; i < WOOL_COLOR_COUNT; ++i) a[i] = buildSheepModel(Sheep::woolRgb(static_cast<WoolColor>(i)));
        return a;
    }();
    return models[std::min(static_cast<int>(c), WOOL_COLOR_COUNT - 1)];
}

// Spawn-time color roll. Mobs are created without a MobContext, so the
// sheep keeps its own generator (main thread only, like all mob code).
std::mt19937& spawnRng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

} // namespace

// ---------------------------------------------------------------------------
// EatGrassGoal
// ---------------------------------------------------------------------------

bool EatGrassGoal::hasGrass(const Mob& mob, const MobContext& ctx) {
    // Small lift so a feet position resting exactly on a block top lands in
    // the air block above it.
    glm::ivec3 b = MobContext::blockAtPos(mob.pos + glm::vec3(0.0f, 0.05f, 0.0f));
    return ctx.blocks->blockAt(b.x, b.y, b.z) == TALL_GRASS || ctx.blocks->blockAt(b.x, b.y - 1, b.z) == GRASS;
}

bool EatGrassGoal::canStart(Mob& mob, MobContext& ctx) {
    if (!mob.onGround || mob.inWater) return false;
    return ctx.chance(chancePerTick) && hasGrass(mob, ctx);
}

bool EatGrassGoal::shouldContinue(Mob& /*mob*/, MobContext& /*ctx*/) {
    return sheep.eatAnimTicks > 0;
}

void EatGrassGoal::start(Mob& mob, MobContext& /*ctx*/) {
    sheep.eatAnimTicks = DURATION_TICKS;
    mob.stopNavigation();
}

void EatGrassGoal::stop(Mob& /*mob*/, MobContext& /*ctx*/) {
    sheep.eatAnimTicks = 0;
}

void EatGrassGoal::tick(Mob& mob, MobContext& ctx) {
    sheep.eatAnimTicks = std::max(0, sheep.eatAnimTicks - 1);
    if (sheep.eatAnimTicks != EAT_AT_TICK) return;
    glm::ivec3 b = MobContext::blockAtPos(mob.pos + glm::vec3(0.0f, 0.05f, 0.0f));
    if (ctx.blocks->blockAt(b.x, b.y, b.z) == TALL_GRASS) {
        // Cross plants don't block light, so a plain swap needs no relight.
        if (ctx.world) ctx.world->setBlock(b.x, b.y, b.z, AIR);
        sheep.regrowWool();
    } else if (ctx.blocks->blockAt(b.x, b.y - 1, b.z) == GRASS) {
        // Opaque → opaque: setBlock marks the section (+ border neighbors)
        // dirty and flags the chunk modified for saving; light is unchanged.
        if (ctx.world) ctx.world->setBlock(b.x, b.y - 1, b.z, DIRT);
        sheep.regrowWool();
    }
}

// ---------------------------------------------------------------------------
// Sheep
// ---------------------------------------------------------------------------

Sheep::Sheep() : Mob(MobType::Sheep), color(randomWoolColor(spawnRng())) {
    goals.add(0, std::make_unique<PanicGoal>(PANIC_SPEED));
    goals.add(5, std::make_unique<EatGrassGoal>(*this));
    goals.add(6, std::make_unique<WanderGoal>(WANDER_SPEED));
    goals.add(7, std::make_unique<LookAtPlayerGoal>(6.0f));
    goals.add(8, std::make_unique<RandomLookGoal>());
}

bool Sheep::shear() {
    if (sheared || !isAlive()) return false;
    sheared = true;
    return true;
}

WoolColor Sheep::randomWoolColor(std::mt19937& rng) {
    // Same roll as Minecraft's Sheep.getRandomSheepColor.
    int roll = std::uniform_int_distribution<int>(0, 99)(rng);
    if (roll < 5) return WoolColor::Black;
    if (roll < 10) return WoolColor::Gray;
    if (roll < 15) return WoolColor::LightGray;
    if (roll < 18) return WoolColor::Brown;
    return std::uniform_int_distribution<int>(0, 499)(rng) == 0 ? WoolColor::Pink : WoolColor::White;
}

glm::vec3 Sheep::woolRgb(WoolColor c) {
    switch (c) {
    case WoolColor::White: return {0.95f, 0.95f, 0.95f};
    case WoolColor::LightGray: return {0.62f, 0.62f, 0.60f};
    case WoolColor::Gray: return {0.31f, 0.32f, 0.34f};
    case WoolColor::Black: return {0.10f, 0.10f, 0.12f};
    case WoolColor::Brown: return {0.45f, 0.30f, 0.18f};
    case WoolColor::Pink: return {0.95f, 0.63f, 0.74f};
    case WoolColor::Count: break;
    }
    return {0.95f, 0.95f, 0.95f};
}

float Sheep::headEatDrop(float partialTick) const {
    // Minecraft's getHeadEatPositionScale: ease down over 4 ticks, hold,
    // ease back up over the last 4.
    float t = static_cast<float>(eatAnimTicks);
    if (eatAnimTicks <= 0) return 0.0f;
    if (eatAnimTicks >= 4 && eatAnimTicks <= 36) return 1.0f;
    float v = eatAnimTicks < 4 ? (t - partialTick) / 4.0f : -((t - 40.0f) - partialTick) / 4.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

float Sheep::headEatPitch(float partialTick, float lookPitchRad) const {
    // Minecraft's getHeadEatAngleScale: nibbling bob while the head is down.
    if (eatAnimTicks > 4 && eatAnimTicks <= 36) {
        float f = (static_cast<float>(eatAnimTicks - 4) - partialTick) / 32.0f;
        return glm::pi<float>() / 5.0f + 0.21991149f * std::sin(f * 28.7f);
    }
    if (eatAnimTicks > 0) return glm::pi<float>() / 5.0f;
    return lookPitchRad;
}

const MobModel& Sheep::model() const {
    return sheepModel(color).model;
}

MobPose Sheep::pose(float partialTick) const {
    const SheepModel& sm = sheepModel(color);
    MobPose p;
    glm::vec2 anim = walkAnim(partialTick);
    poseQuadrupedLegs(p, sm.frontLeft, sm.frontRight, sm.backLeft, sm.backRight, anim.x, anim.y);
    float relYaw = wrapDegrees(renderHeadYaw(partialTick) - renderYaw(partialTick));
    if (eatAnimTicks > 0) {
        // Grazing: drop the neck 9 px and tip the face toward the grass.
        poseHeadLook(p, sm.head, relYaw, 0.0f);
        p.rotation[sm.head].x = headEatPitch(partialTick, 0.0f);
        p.offset[sm.head].y = -9.0f * PX * headEatDrop(partialTick);
    } else {
        poseHeadLook(p, sm.head, relYaw, renderHeadPitch(partialTick));
    }
    if (sheared)
        for (int idx : sm.wool) p.hidden[idx] = true;
    return p;
}
