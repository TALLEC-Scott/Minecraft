#include "tnt_entity.h"

#include <cmath>
#include <random>

#include "collision.h"
#include "cube.h"
#include "world.h"
#include "world_resolver.h"

namespace {

// Thread-safe enough for single-threaded game loop; tests reseed explicitly.
std::mt19937& rng() {
    static std::mt19937 g(12345u);
    return g;
}

float rand01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng());
}

} // namespace

TntEntity::TntEntity(glm::vec3 spawnPos, int fuse, double now)
    : pos(spawnPos), velocity(0.0f), fuseTicks(fuse), spawnTime(now) {
    // Matches Minecraft: +0.20 y + 0.02-magnitude random horizontal
    float angle = rand01() * 6.2831853f;
    velocity = glm::vec3(std::cos(angle) * 0.02f, 0.20f, std::sin(angle) * 0.02f);
}

bool TntEntity::tick(World* world) {
    velocity.y -= TICK_GRAVITY;

    if (world) {
        // Solid check through the chunk manager. Water and air are non-solid;
        // TNT bodies themselves don't occupy the world as blocks (the source
        // block was replaced with AIR on ignition), so we don't need to
        // exclude self.
        WorldResolver resolver(world->chunkManager);
        auto isSolid = [&](int bx, int by, int bz) -> bool {
            if (by < 0 || by >= CHUNK_HEIGHT) return by < 0; // treat below-world as solid floor
            auto loc = resolver.local(bx, bz);
            if (!loc.chunk) return false;
            block_type bt = loc.chunk->getBlockType(loc.lx, by, loc.lz);
            return hasFlag(bt, BF_SOLID);
        };

        // Feet position = center-bottom of the AABB, matching the
        // resolveVertical/resolveMovement convention in collision.h.
        glm::vec3 feet = glm::vec3(pos.x, pos.y - HEIGHT * 0.5f, pos.z);

        // Horizontal move (with wall slide) first, then vertical.
        glm::vec3 hmove(velocity.x, 0.0f, velocity.z);
        if (glm::dot(hmove, hmove) > 0.0f) {
            glm::vec3 resolved = resolveMovementAABB(feet, hmove, HALF_WIDTH, HEIGHT, isSolid);
            // If we hit a wall on an axis, zero that velocity component.
            if (std::abs(resolved.x - feet.x - hmove.x) > 0.0001f) velocity.x = 0.0f;
            if (std::abs(resolved.z - feet.z - hmove.z) > 0.0001f) velocity.z = 0.0f;
            feet.x = resolved.x;
            feet.z = resolved.z;
        }

        VerticalResult vr = resolveVerticalAABB(feet, velocity.y, HALF_WIDTH, HEIGHT, isSolid);
        feet.y = vr.newFeetY;
        if (vr.hitGround) {
            velocity.y = 0.0f;
            onGround = true;
        } else {
            onGround = false;
        }
        if (vr.hitCeiling) velocity.y = 0.0f;

        pos = glm::vec3(feet.x, feet.y + HEIGHT * 0.5f, feet.z);
    } else {
        pos += velocity;
    }

    // Drag: slight vertical damping; stronger horizontal friction when on
    // ground so TNT doesn't slide forever after spawn impulse.
    velocity.y *= DRAG;
    if (onGround) {
        velocity.x *= GROUND_FRICTION;
        velocity.z *= GROUND_FRICTION;
    }

    --fuseTicks;
    return fuseTicks <= 0;
}

float TntEntity::flashBrightness(double now) const {
    // Flash pulses faster as fuse runs out. Last 40 ticks (~2s) go ~5 Hz.
    double age = now - spawnTime;
    if (age < 0) age = 0;
    double rate = (fuseTicks < 40) ? 10.0 : 4.0;
    double phase = std::fmod(age * rate, 1.0);
    // Soft pulse: 1.0 base, peaks near 2.4 at phase 0.5.
    float pulse = 1.0f + 1.4f * static_cast<float>(std::sin(phase * 3.14159265f));
    return pulse;
}
