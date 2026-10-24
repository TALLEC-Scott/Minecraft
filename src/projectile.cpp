#include "projectile.h"

#include <algorithm>
#include <cmath>

#include "collision.h"
#include "mob.h"

glm::vec3 Projectile::direction() const {
    return lastDir;
}

void Projectile::tick(MobContext& ctx, std::vector<std::unique_ptr<Mob>>* mobs) {
    prevPos = pos;
    ++age;
    if (removed) return;
    if (stuck) {
        if (age >= STUCK_TICKS || age >= spec.lifetimeTicks) removed = true;
        return;
    }
    if (age >= spec.lifetimeTicks) {
        removed = true;
        return;
    }

    float len = glm::length(velocity);
    if (len > 1e-6f) lastDir = velocity / len;

    // Entity hits along this tick's segment: nearest of player / mobs.
    float bestT = 1.0f;
    Mob* hitMob = nullptr;
    bool hitPlayer = false;
    float t = 0.0f;
    if (spec.hurtsPlayer && ctx.playerTargetable) {
        glm::vec3 mn(ctx.playerFeet.x - PLAYER_BOX_HALF_WIDTH, ctx.playerFeet.y,
                     ctx.playerFeet.z - PLAYER_BOX_HALF_WIDTH);
        glm::vec3 mx(ctx.playerFeet.x + PLAYER_BOX_HALF_WIDTH, ctx.playerFeet.y + PLAYER_BOX_HEIGHT,
                     ctx.playerFeet.z + PLAYER_BOX_HALF_WIDTH);
        if (rayIntersectsAABB(pos, velocity, mn, mx, bestT, t)) {
            bestT = t;
            hitPlayer = true;
        }
    }
    if (spec.hurtsMobs && mobs) {
        for (auto& m : *mobs) {
            if (!m->isAlive() || m->id == spec.shooterId) continue;
            if (m->rayHit(pos, velocity, bestT, t)) {
                bestT = t;
                hitMob = m.get();
                hitPlayer = false;
            }
        }
    }

    // Block hit: march the segment in quarter-block steps (velocity is
    // capped well below a block per step for arrows at ~3 blocks/tick).
    int steps = std::max(1, static_cast<int>(std::ceil(len / 0.25f)));
    for (int i = 1; i <= steps; ++i) {
        float f = static_cast<float>(i) / static_cast<float>(steps);
        if (f > bestT && (hitPlayer || hitMob)) break; // entity is closer
        glm::ivec3 b = MobContext::blockAtPos(pos + velocity * f);
        if (b.y >= 0 && b.y < CHUNK_HEIGHT && ctx.isSolid(b.x, b.y, b.z)) {
            pos += velocity * (static_cast<float>(i - 1) / static_cast<float>(steps));
            velocity = glm::vec3(0.0f);
            stuck = true;
            age = 0; // reuse as stuck timer
            return;
        }
    }

    if (hitPlayer) {
        ctx.damagePlayer(spec.damage, pos);
        removed = true;
        return;
    }
    if (hitMob) {
        hitMob->damage(spec.damage, pos - velocity, 0.5f);
        removed = true;
        return;
    }

    pos += velocity;
    velocity = velocity * spec.drag;
    velocity.y -= spec.gravity;
    if (pos.y < -16.0f) removed = true;
}
