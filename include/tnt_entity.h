#pragma once

#include <glm/glm.hpp>

class World;

// Primed TNT — spawned when a TNT block is ignited. Minecraft-style:
//   - 80-tick default fuse (4s @ 20TPS); chain-ignited uses 10-30 ticks
//   - +0.2 upward and 0.02-magnitude horizontal spawn impulse
//   - gravity + voxel collision until fuse hits zero
//   - visual flashes via flashBrightness() as the fuse nears zero
class TntEntity {
  public:
    glm::vec3 pos;      // block-centered world position (e.g. 5.5, 10.5, 5.5)
    glm::vec3 velocity; // blocks per tick
    int fuseTicks;
    double spawnTime; // glfwGetTime() at spawn; drives flashing. 0 when spawned from tests.
    bool onGround = false;
    bool dead = false;

    // All physics constants are per-tick (20 TPS). Using Minecraft's published
    // values so behaviour matches the reference description.
    static constexpr float TICK_GRAVITY = 0.04f;    // blocks / tick²
    static constexpr float DRAG = 0.98f;       // per-tick vertical velocity retention
    static constexpr float GROUND_FRICTION = 0.7f; // per-tick horizontal retention on ground
    static constexpr int DEFAULT_FUSE = 80;
    static constexpr int CHAIN_FUSE_MIN = 10;
    static constexpr int CHAIN_FUSE_MAX = 30;
    // Hitbox slightly under 1 block so it doesn't self-trap when spawned at
    // block-center coordinates above a solid floor.
    static constexpr float HALF_WIDTH = 0.49f;
    static constexpr float HEIGHT = 0.98f;

    TntEntity(glm::vec3 spawnPos, int fuse, double now);

    // One 20-TPS tick. `world` may be null for unit tests that exercise fuse
    // countdown / spawn-impulse math without a live world. Returns true when
    // the fuse has just reached zero — the caller should detonate now.
    bool tick(World* world);

    // Brightness multiplier used by the flashing effect. In the final 40
    // ticks (~2 s) the pulse rate jumps from 4 Hz to 10 Hz as Minecraft-
    // style visual signalling that detonation is imminent.
    float flashBrightness(double now) const;
};
