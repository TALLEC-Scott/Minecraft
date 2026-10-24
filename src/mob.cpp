#include "mob.h"

#include <algorithm>
#include <cmath>

#include "collision.h"

// ---------------------------------------------------------------------------
// MobContext
// ---------------------------------------------------------------------------

bool MobContext::isSolid(int x, int y, int z) const {
    if (y < 0) return true; // below-world floor
    if (y >= CHUNK_HEIGHT) return false;
    if (!blocks->isLoaded(x, z)) return true; // streaming edge acts as a wall
    return hasFlag(blocks->blockAt(x, y, z), BF_SOLID);
}

bool MobContext::isWater(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    return blocks->blockAt(x, y, z) == WATER;
}

int MobContext::lightAt(int x, int y, int z) const {
    if (y >= CHUNK_HEIGHT) return std::max(0, 15 - skyDarken);
    if (y < 0) return 0;
    int sky = blocks->skyLightAt(x, y, z) - skyDarken;
    return std::max(blocks->blockLightAt(x, y, z), std::max(sky, 0));
}

glm::ivec3 MobContext::blockAtPos(glm::vec3 p) {
    // Block N spans [N-0.5, N+0.5) — shift by half a block before flooring.
    return {static_cast<int>(std::floor(p.x + 0.5f)), static_cast<int>(std::floor(p.y + 0.5f)),
            static_cast<int>(std::floor(p.z + 0.5f))};
}

float MobContext::randf() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(*rng);
}

int MobContext::randInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return std::uniform_int_distribution<int>(lo, hi)(*rng);
}

// ---------------------------------------------------------------------------
// Angle helpers
// ---------------------------------------------------------------------------

float wrapDegrees(float deg) {
    deg = std::fmod(deg, 360.0f);
    if (deg >= 180.0f) deg -= 360.0f;
    if (deg < -180.0f) deg += 360.0f;
    return deg;
}

float approachAngle(float from, float to, float maxStep) {
    float d = std::clamp(wrapDegrees(to - from), -maxStep, maxStep);
    return wrapDegrees(from + d);
}

float yawFromDirection(float dx, float dz) {
    return glm::degrees(std::atan2(dz, dx));
}

// ---------------------------------------------------------------------------
// Mob
// ---------------------------------------------------------------------------

Mob::Mob(MobType type)
    : type(type), halfWidth(mobInfo(type).halfWidth), height(mobInfo(type).height),
      health(mobInfo(type).maxHealth), maxHealth(mobInfo(type).maxHealth) {}

glm::vec3 Mob::forward() const {
    float r = glm::radians(yaw);
    return {std::cos(r), 0.0f, std::sin(r)};
}

void Mob::navigateTo(glm::vec3 target, float speed, bool avoid) {
    if (!hasMoveTarget || glm::distance(target, moveTarget) > 0.5f) stuckTicks = 0;
    hasMoveTarget = true;
    moveTarget = target;
    moveSpeed = speed;
    avoidDrops = avoid;
}

void Mob::stopNavigation() {
    hasMoveTarget = false;
    stuckTicks = 0;
}

float Mob::distanceToMoveTarget() const {
    if (!hasMoveTarget) return 0.0f;
    return glm::length(glm::vec2(moveTarget.x - pos.x, moveTarget.z - pos.z));
}

void Mob::lookAt(glm::vec3 point, float maxYawStep, float maxPitchStep) {
    glm::vec3 d = point - eyePos();
    float horiz = std::sqrt(d.x * d.x + d.z * d.z);
    if (horiz > 1e-4f) headYaw = approachAngle(headYaw, yawFromDirection(d.x, d.z), maxYawStep);
    float targetPitch = std::clamp(glm::degrees(std::atan2(d.y, std::max(horiz, 1e-4f))), -60.0f, 60.0f);
    headPitch += std::clamp(targetPitch - headPitch, -maxPitchStep, maxPitchStep);
    lookedThisTick = true;
}

void Mob::aiTick(MobContext& ctx) {
    goals.tick(*this, ctx);
}

void Mob::tick(MobContext& ctx) {
    prevPos = pos;
    prevYaw = yaw;
    prevHeadYaw = headYaw;
    prevHeadPitch = headPitch;
    prevWalkAnimPos = walkAnimPos;
    prevWalkAnimSpeed = walkAnimSpeed;
    ++ageTicks;
    lookedThisTick = false;

    updateWater(ctx);

    if (!dying) {
        aiTick(ctx);
        hurtSinceLastAi = false;

        int interval = ambientSoundInterval();
        if (interval > 0) {
            if (ambientCooldown < 0) ambientCooldown = ctx.randInt(interval / 2, interval * 2);
            if (--ambientCooldown <= 0) {
                queueSound(MobSoundEvent::Ambient);
                ambientCooldown = interval + ctx.randInt(0, interval);
            }
        }
    } else {
        hasMoveTarget = false;
    }

    physicsTick(ctx);

    // Head: relax back to the body when no goal aimed it this tick, and
    // never let it twist more than 75° off the body (the body follows
    // instead — Minecraft's BodyRotationControl in miniature).
    if (!lookedThisTick) {
        headYaw = approachAngle(headYaw, yaw, 10.0f);
        headPitch += std::clamp(-headPitch, -10.0f, 10.0f);
    }
    float rel = wrapDegrees(headYaw - yaw);
    if (rel > 75.0f) yaw = wrapDegrees(headYaw - 75.0f);
    if (rel < -75.0f) yaw = wrapDegrees(headYaw + 75.0f);

    customTick(ctx);

    if (hurtTicks > 0) --hurtTicks;
    if (invulnerableTicks > 0) --invulnerableTicks;
    if (armSwingTicks > 0) --armSwingTicks;
    if (dying && ++deathTicks >= DEATH_TICKS) removed = true;

    // Limb swing (Minecraft WalkAnimationState): speed tracks horizontal
    // distance moved this tick, eased so legs don't snap.
    float dx = pos.x - prevPos.x, dz = pos.z - prevPos.z;
    float moved = std::sqrt(dx * dx + dz * dz);
    float target = dying ? 0.0f : std::min(moved * 4.0f, 1.0f);
    walkAnimSpeed += (target - walkAnimSpeed) * 0.4f;
    walkAnimPos += walkAnimSpeed;

    // Navigation progress — goals give up when stuck.
    if (hasMoveTarget && moved < moveSpeed * 0.25f)
        ++stuckTicks;
    else
        stuckTicks = 0;

    if (onGround && !inWater) {
        stepDistance += moved;
        if (stepDistance > 1.6f) {
            stepDistance = 0.0f;
            queueSound(MobSoundEvent::Step);
        }
    }
}

void Mob::updateWater(MobContext& ctx) {
    glm::ivec3 feet = MobContext::blockAtPos(pos + glm::vec3(0.0f, 0.1f, 0.0f));
    glm::ivec3 mid = MobContext::blockAtPos(center());
    inWater = ctx.isWater(feet.x, feet.y, feet.z) || ctx.isWater(mid.x, mid.y, mid.z);
    if (inWater) fallDistance = 0.0f;
}

bool Mob::dropAhead(MobContext& ctx, glm::vec3 dir) const {
    glm::vec3 probe = pos + dir * (halfWidth + 0.5f);
    glm::ivec3 b = MobContext::blockAtPos(probe);
    int feetY = MobContext::blockAtPos(pos).y;
    // Solid at feet level = a ledge to step onto, not a drop.
    if (ctx.isSolid(b.x, feetY, b.z)) return false;
    for (int y = feetY - 1; y >= feetY - 4; --y) {
        if (ctx.isWater(b.x, y, b.z)) return true;
        if (ctx.isSolid(b.x, y, b.z)) return (feetY - 1 - y) > 3;
    }
    return true;
}

glm::vec3 Mob::steeringVelocity(MobContext& ctx) {
    if (!hasMoveTarget) return glm::vec3(0.0f);
    glm::vec2 to(moveTarget.x - pos.x, moveTarget.z - pos.z);
    float dist = glm::length(to);
    if (dist < 0.3f) {
        hasMoveTarget = false; // arrived
        return glm::vec3(0.0f);
    }
    glm::vec2 dir = to / dist;
    if (avoidDrops && onGround && !inWater && dropAhead(ctx, glm::vec3(dir.x, 0.0f, dir.y))) {
        hasMoveTarget = false;
        return glm::vec3(0.0f);
    }
    yaw = approachAngle(yaw, yawFromDirection(dir.x, dir.y), 30.0f);
    float speed = std::min(moveSpeed, dist);
    return {dir.x * speed, 0.0f, dir.y * speed};
}

void Mob::physicsTick(MobContext& ctx) {
    glm::vec3 desired = dying ? glm::vec3(0.0f) : steeringVelocity(ctx);
    bool moving = glm::dot(desired, desired) > 1e-8f;

    if (inWater) {
        // Half-speed swimming; float toward the surface while the body
        // center is submerged.
        constexpr float a = (1.0f - WATER_SLIP) / WATER_SLIP * 0.5f;
        velocity.x += desired.x * a;
        velocity.z += desired.z * a;
        glm::ivec3 c = MobContext::blockAtPos(pos + glm::vec3(0.0f, height * 0.6f, 0.0f));
        if (!dying && floatsInWater() && ctx.isWater(c.x, c.y, c.z)) velocity.y += SWIM_UP;
        // Climb out onto a shore ledge.
        if (!dying && moving && horizontalCollision) velocity.y = std::max(velocity.y, 0.3f);
    } else {
        float slip = onGround ? GROUND_SLIP : AIR_SLIP;
        // Input acceleration chosen so terminal speed == desired after
        // the post-move friction multiply.
        float a = (1.0f - slip) / slip;
        velocity.x += desired.x * a;
        velocity.z += desired.z * a;

        if (onGround && moving) {
            // Auto step-up: a solid block at feet level ahead with head
            // room above it → jump.
            glm::vec3 dir = glm::normalize(desired);
            glm::ivec3 ahead = MobContext::blockAtPos(pos + dir * (halfWidth + 0.35f));
            int feetY = MobContext::blockAtPos(pos).y;
            int clearance = static_cast<int>(std::ceil(height));
            if (ctx.isSolid(ahead.x, feetY, ahead.z)) {
                bool room = true;
                for (int dy = 1; dy <= clearance; ++dy) room = room && !ctx.isSolid(ahead.x, feetY + dy, ahead.z);
                if (room) wantJump = true;
            }
            if (horizontalCollision) wantJump = true;
        }
        if (wantJump && onGround) {
            velocity.y = JUMP_SPEED;
            onGround = false;
        }
    }
    wantJump = false;

    moveWithCollision(ctx, velocity);

    if (inWater) {
        velocity *= WATER_SLIP;
        velocity.y -= WATER_GRAVITY;
    } else {
        float slip = onGround ? GROUND_SLIP : AIR_SLIP;
        velocity.x *= slip;
        velocity.z *= slip;
        velocity.y = (velocity.y - TICK_GRAVITY) * AIR_DRAG;
    }
}

void Mob::moveWithCollision(MobContext& ctx, glm::vec3 delta) {
    auto solid = [&ctx](int x, int y, int z) { return ctx.isSolid(x, y, z); };

    horizontalCollision = false;
    float hLen = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    if (hLen > 1e-6f) {
        // Sub-step long moves (knockback) so a thin wall can't be tunnelled.
        int steps = std::max(1, static_cast<int>(std::ceil(hLen / MAX_STEP_MOVE)));
        glm::vec3 step(delta.x / steps, 0.0f, delta.z / steps);
        bool hitX = false, hitZ = false;
        for (int i = 0; i < steps; ++i) {
            glm::vec3 r = resolveMovementAABB(pos, step, halfWidth, height, solid);
            if (std::abs(r.x - (pos.x + step.x)) > 1e-5f) hitX = true;
            if (std::abs(r.z - (pos.z + step.z)) > 1e-5f) hitZ = true;
            pos.x = r.x;
            pos.z = r.z;
        }
        horizontalCollision = hitX || hitZ;
        // Navigating mobs keep pressing (so a step-up jump carries them
        // over the ledge); otherwise a wall kills that velocity axis.
        if (!hasMoveTarget) {
            if (hitX) velocity.x = 0.0f;
            if (hitZ) velocity.z = 0.0f;
        }
    }

    VerticalResult vr = resolveVerticalAABB(pos, delta.y, halfWidth, height, solid);
    float dy = vr.newFeetY - pos.y;
    pos.y = vr.newFeetY;
    if (vr.hitCeiling) velocity.y = 0.0f;

    bool wasOnGround = onGround;
    onGround = vr.hitGround;
    if (onGround) {
        velocity.y = 0.0f;
        if (!wasOnGround || fallDistance > 0.0f) {
            float dmg = std::ceil(fallDistance - SAFE_FALL) * fallDamageMultiplier();
            if (dmg > 0.0f && !inWater) damage(dmg, pos, 0.0f);
        }
        fallDistance = 0.0f;
    } else if (dy < 0.0f && !inWater) {
        fallDistance -= dy;
    }
}

bool Mob::damage(float amount, glm::vec3 sourcePos, float knockbackScale) {
    if (dying || removed || amount <= 0.0f) return false;
    if (invulnerableTicks > 0) return false;

    health -= amount;
    hurtTicks = HURT_FLASH_TICKS;
    invulnerableTicks = INVULNERABLE_TICKS;
    hurtSinceLastAi = true;
    lastHurtFrom = sourcePos;

    if (knockbackScale > 0.0f) {
        // Minecraft LivingEntity.knockback: halve current velocity, add a
        // fixed push away from the source, small hop when grounded.
        glm::vec2 away(pos.x - sourcePos.x, pos.z - sourcePos.z);
        float len = glm::length(away);
        if (len < 1e-4f) {
            glm::vec3 f = forward();
            away = glm::vec2(-f.x, -f.z);
        } else {
            away /= len;
        }
        float k = KNOCKBACK * knockbackScale;
        velocity.x = velocity.x * 0.5f + away.x * k;
        velocity.z = velocity.z * 0.5f + away.y * k;
        if (onGround) velocity.y = std::min(0.4f, velocity.y * 0.5f + k);
    }

    onHurt(amount, sourcePos);
    if (health <= 0.0f) {
        health = 0.0f;
        dying = true;
        deathTicks = 0;
        hasMoveTarget = false;
        queueSound(MobSoundEvent::Death);
        onDeath();
    } else {
        queueSound(MobSoundEvent::Hurt);
    }
    return true;
}

bool Mob::rayHit(glm::vec3 origin, glm::vec3 dir, float maxDist, float& outT) const {
    glm::vec3 mn(pos.x - halfWidth, pos.y, pos.z - halfWidth);
    glm::vec3 mx(pos.x + halfWidth, pos.y + height, pos.z + halfWidth);
    return rayIntersectsAABB(origin, dir, mn, mx, maxDist, outT);
}

float Mob::renderYaw(float pt) const {
    return prevYaw + wrapDegrees(yaw - prevYaw) * pt;
}

float Mob::renderHeadYaw(float pt) const {
    return prevHeadYaw + wrapDegrees(headYaw - prevHeadYaw) * pt;
}

float Mob::renderHeadPitch(float pt) const {
    return prevHeadPitch + (headPitch - prevHeadPitch) * pt;
}

glm::vec2 Mob::walkAnim(float pt) const {
    float speed = prevWalkAnimSpeed + (walkAnimSpeed - prevWalkAnimSpeed) * pt;
    float animPos = walkAnimPos - walkAnimSpeed * (1.0f - pt);
    return {animPos, std::min(speed, 1.0f)};
}

float Mob::deathProgress(float pt) const {
    if (!dying) return 0.0f;
    float f = (static_cast<float>(deathTicks) + pt - 1.0f) / static_cast<float>(DEATH_TICKS) * 1.6f;
    return std::min(std::sqrt(std::max(f, 0.0f)), 1.0f);
}

float Mob::armSwingProgress(float pt) const {
    if (armSwingTicks <= 0) return 0.0f;
    float t = (static_cast<float>(ARM_SWING_TICKS - armSwingTicks) + pt) / static_cast<float>(ARM_SWING_TICKS);
    return std::clamp(t, 0.0f, 1.0f);
}
