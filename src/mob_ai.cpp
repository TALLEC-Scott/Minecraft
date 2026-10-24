#include "mob_ai.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "mob.h"

namespace {
constexpr float PLAYER_WIDTH = 0.6f;
}

// ---------------------------------------------------------------------------
// GoalSelector
// ---------------------------------------------------------------------------

void GoalSelector::add(int priority, std::unique_ptr<MobGoal> goal) {
    // Stable insert: equal priorities keep registration order.
    auto it = std::upper_bound(entries.begin(), entries.end(), priority,
                               [](int p, const Entry& e) { return p < e.priority; });
    entries.insert(it, Entry{priority, std::move(goal), false});
}

void GoalSelector::tick(Mob& mob, MobContext& ctx) {
    for (auto& e : entries) {
        if (e.running && !e.goal->shouldContinue(mob, ctx)) {
            e.goal->stop(mob, ctx);
            e.running = false;
        }
    }

    // Entries are sorted most-important first, so a goal started here
    // blocks every later (less important) goal sharing a channel.
    for (auto& e : entries) {
        if (e.running) continue;
        bool blocked = false;
        for (const auto& o : entries) {
            if (o.running && (o.goal->channels & e.goal->channels) && o.priority <= e.priority) {
                blocked = true;
                break;
            }
        }
        if (blocked || !e.goal->canStart(mob, ctx)) continue;
        for (auto& o : entries) {
            if (o.running && (o.goal->channels & e.goal->channels)) {
                o.goal->stop(mob, ctx);
                o.running = false;
            }
        }
        e.goal->start(mob, ctx);
        e.running = true;
    }

    for (auto& e : entries)
        if (e.running) e.goal->tick(mob, ctx);
}

void GoalSelector::stopAll(Mob& mob, MobContext& ctx) {
    for (auto& e : entries) {
        if (!e.running) continue;
        e.goal->stop(mob, ctx);
        e.running = false;
    }
}

// ---------------------------------------------------------------------------
// Terrain helpers
// ---------------------------------------------------------------------------

namespace mobai {

bool findStandY(const MobContext& ctx, int x, int z, int nearY, float /*halfWidth*/, float height, int up, int down,
                glm::vec3& outFeet) {
    if (!ctx.blocks->isLoaded(x, z)) return false;
    int clearance = std::max(1, static_cast<int>(std::ceil(height)));
    // Prefer spots closest to the current level: 0, +1, -1, +2, -2, …
    int range = std::max(up, down);
    for (int i = 0; i <= range * 2; ++i) {
        int dy = (i % 2 == 0) ? -(i / 2) : (i + 1) / 2;
        if (dy > up || -dy > down) continue;
        int y = nearY + dy; // candidate feet block
        if (y < 1 || y + clearance >= CHUNK_HEIGHT) continue;
        if (!ctx.isSolid(x, y - 1, z)) continue;
        bool room = true;
        for (int c = 0; c < clearance && room; ++c) room = !ctx.isSolid(x, y + c, z);
        if (!room) continue;
        // Block y-1's top face is at y - 0.5.
        outFeet = glm::vec3(static_cast<float>(x), static_cast<float>(y) - 0.5f, static_cast<float>(z));
        return true;
    }
    return false;
}

bool randomStandTarget(Mob& mob, MobContext& ctx, float radius, bool avoidWater, glm::vec3& out, glm::vec3 awayFrom,
                       bool biasAway) {
    float awayAngle = 0.0f;
    if (biasAway) {
        glm::vec2 d(mob.pos.x - awayFrom.x, mob.pos.z - awayFrom.z);
        awayAngle = (glm::dot(d, d) > 1e-6f) ? std::atan2(d.y, d.x) : ctx.randf() * glm::two_pi<float>();
    }
    int feetY = MobContext::blockAtPos(mob.pos).y;
    for (int attempt = 0; attempt < 10; ++attempt) {
        float ang = biasAway ? awayAngle + (ctx.randf() - 0.5f) * glm::pi<float>()
                             : ctx.randf() * glm::two_pi<float>();
        float r = 2.0f + ctx.randf() * std::max(radius - 2.0f, 0.0f);
        int x = static_cast<int>(std::floor(mob.pos.x + std::cos(ang) * r + 0.5f));
        int z = static_cast<int>(std::floor(mob.pos.z + std::sin(ang) * r + 0.5f));
        glm::vec3 feet;
        if (!findStandY(ctx, x, z, feetY, mob.halfWidth, mob.height, 4, 4, feet)) continue;
        int fy = MobContext::blockAtPos(feet).y;
        if (avoidWater && ctx.isWater(x, fy, z)) continue;
        out = feet;
        return true;
    }
    return false;
}

bool hasLineOfSight(const MobContext& ctx, glm::vec3 from, glm::vec3 to) {
    // Amanatides–Woo grid walk. Block N spans [N-0.5, N+0.5), so shift by
    // half a block into a plain floor() grid first.
    glm::vec3 a = from + 0.5f;
    glm::vec3 d = (to + 0.5f) - a;
    glm::ivec3 cell(glm::floor(a));
    glm::ivec3 end(glm::floor(a + d));
    glm::ivec3 step(0);
    glm::vec3 tMax(2.0f), tDelta(0.0f); // t in [0, 1] along the segment; > 1 = never
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9f) continue;
        step[i] = d[i] > 0.0f ? 1 : -1;
        float boundary = static_cast<float>(cell[i] + (step[i] > 0 ? 1 : 0));
        tMax[i] = (boundary - a[i]) / d[i];
        tDelta[i] = 1.0f / std::abs(d[i]);
    }
    int maxSteps = std::abs(end.x - cell.x) + std::abs(end.y - cell.y) + std::abs(end.z - cell.z) + 1;
    for (int n = 0; n < maxSteps; ++n) {
        if (ctx.isSolid(cell.x, cell.y, cell.z)) return false;
        if (cell == end) break;
        int axis = (tMax.x < tMax.y) ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        if (tMax[axis] > 1.0f) break;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
    return true;
}

} // namespace mobai

// ---------------------------------------------------------------------------
// WanderGoal
// ---------------------------------------------------------------------------

bool WanderGoal::canStart(Mob& mob, MobContext& ctx) {
    if (!ctx.chance(chancePerTick)) return false;
    return mobai::randomStandTarget(mob, ctx, radius, true, target);
}

bool WanderGoal::shouldContinue(Mob& mob, MobContext& /*ctx*/) {
    return mob.isNavigating() && mob.navStuckTicks() < 40 && ticksRunning < 400;
}

void WanderGoal::start(Mob& mob, MobContext& /*ctx*/) {
    ticksRunning = 0;
    mob.navigateTo(target, speed, true);
}

void WanderGoal::stop(Mob& mob, MobContext& /*ctx*/) {
    mob.stopNavigation();
}

// ---------------------------------------------------------------------------
// PanicGoal
// ---------------------------------------------------------------------------

bool PanicGoal::canStart(Mob& mob, MobContext& /*ctx*/) {
    return mob.hurtSinceLastAi;
}

bool PanicGoal::shouldContinue(Mob& mob, MobContext& /*ctx*/) {
    if (mob.hurtSinceLastAi) remaining = duration; // hit again → keep running
    return remaining > 0;
}

void PanicGoal::start(Mob& mob, MobContext& ctx) {
    remaining = duration;
    pickTarget(mob, ctx);
}

void PanicGoal::stop(Mob& mob, MobContext& /*ctx*/) {
    mob.stopNavigation();
}

void PanicGoal::tick(Mob& mob, MobContext& ctx) {
    --remaining;
    if (!mob.isNavigating() || mob.navStuckTicks() > 10) pickTarget(mob, ctx);
}

void PanicGoal::pickTarget(Mob& mob, MobContext& ctx) {
    glm::vec3 target;
    if (mobai::randomStandTarget(mob, ctx, 8.0f, false, target, mob.lastHurtFrom, true)) {
        mob.navigateTo(target, speed, true);
        return;
    }
    // Boxed in: bolt in a random direction and let collision sort it out.
    float ang = ctx.randf() * glm::two_pi<float>();
    mob.navigateTo(mob.pos + glm::vec3(std::cos(ang), 0.0f, std::sin(ang)) * 5.0f, speed, true);
}

// ---------------------------------------------------------------------------
// LookAtPlayerGoal / RandomLookGoal
// ---------------------------------------------------------------------------

bool LookAtPlayerGoal::canStart(Mob& mob, MobContext& ctx) {
    if (!ctx.playerTargetable || !ctx.chance(chancePerTick)) return false;
    return glm::distance(mob.eyePos(), ctx.playerEye) < range;
}

bool LookAtPlayerGoal::shouldContinue(Mob& mob, MobContext& ctx) {
    return remaining > 0 && ctx.playerTargetable && glm::distance(mob.eyePos(), ctx.playerEye) < range * 1.25f;
}

void LookAtPlayerGoal::start(Mob& /*mob*/, MobContext& ctx) {
    remaining = 40 + ctx.randInt(0, 40);
}

void LookAtPlayerGoal::tick(Mob& mob, MobContext& ctx) {
    --remaining;
    mob.lookAt(ctx.playerEye);
}

bool RandomLookGoal::canStart(Mob& /*mob*/, MobContext& ctx) {
    return ctx.chance(0.02f);
}

void RandomLookGoal::start(Mob& mob, MobContext& ctx) {
    lookYaw = mob.yaw + (ctx.randf() - 0.5f) * 150.0f;
    remaining = 20 + ctx.randInt(0, 20);
}

void RandomLookGoal::tick(Mob& mob, MobContext& /*ctx*/) {
    --remaining;
    float r = glm::radians(lookYaw);
    mob.lookAt(mob.eyePos() + glm::vec3(std::cos(r), 0.0f, std::sin(r)));
}

// ---------------------------------------------------------------------------
// ChaseAndMeleeGoal
// ---------------------------------------------------------------------------

float ChaseAndMeleeGoal::reachSq(const Mob& mob) {
    // Minecraft getMeleeAttackRangeSqr: (width * 2)^2 + target width.
    float w = mob.halfWidth * 2.0f;
    return (w * 2.0f) * (w * 2.0f) + PLAYER_WIDTH;
}

bool ChaseAndMeleeGoal::canStart(Mob& mob, MobContext& ctx) {
    if (!ctx.playerTargetable) return false;
    return glm::distance(mob.pos, ctx.playerFeet) < followRange;
}

bool ChaseAndMeleeGoal::shouldContinue(Mob& mob, MobContext& ctx) {
    if (!ctx.playerTargetable) return false;
    // Keep chasing a little beyond the acquisition range (Minecraft's
    // follow range is generous once locked on).
    return glm::distance(mob.pos, ctx.playerFeet) < followRange * 1.5f;
}

void ChaseAndMeleeGoal::start(Mob& /*mob*/, MobContext& /*ctx*/) {
    repathTicks = 0;
}

void ChaseAndMeleeGoal::stop(Mob& mob, MobContext& /*ctx*/) {
    mob.stopNavigation();
}

void ChaseAndMeleeGoal::tick(Mob& mob, MobContext& ctx) {
    mob.lookAt(ctx.playerEye, 30.0f, 30.0f);
    if (cooldownLeft > 0) --cooldownLeft;

    glm::vec2 d(ctx.playerFeet.x - mob.pos.x, ctx.playerFeet.z - mob.pos.z);
    float distSq = glm::dot(d, d);
    float reach = reachSq(mob);

    // Greedy steering straight at the player; stop short so we don't
    // stand inside them. Re-aim every tick — there's no path to recompute.
    if (distSq > reach * 0.6f)
        mob.navigateTo(ctx.playerFeet, speed, false);
    else
        mob.stopNavigation();

    // Vertical overlap: player spans [feet, feet + 1.8].
    float dy = ctx.playerFeet.y - mob.pos.y;
    bool verticalOk = dy < mob.height + 0.5f && dy > -2.3f;
    if (distSq <= reach && verticalOk && cooldownLeft <= 0) {
        ctx.damagePlayer(damage, mob.pos);
        cooldownLeft = cooldown;
        onAttack(mob, ctx);
    }
}

void ChaseAndMeleeGoal::onAttack(Mob& mob, MobContext& /*ctx*/) {
    mob.swingArm();
    mob.queueSound(MobSoundEvent::Attack);
}
