#pragma once

// Generic arrow-like projectile (skeleton arrows, …). Spawned through
// MobContext::spawnProjectile → MobManager. Per-tick physics in blocks/tick:
// move, then velocity = velocity * drag - gravity. Sticks into the first
// solid block it hits (despawns after STUCK_TICKS), damages the player or
// mobs on contact. GL-free; MobManager renders it as a thin box.

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "mob_context.h"

class Mob;

class Projectile {
  public:
    static constexpr int STUCK_TICKS = 100; // arrow lingers 5 s in a wall
    static constexpr float PLAYER_BOX_HALF_WIDTH = 0.3f;
    static constexpr float PLAYER_BOX_HEIGHT = 1.8f;

    explicit Projectile(const ProjectileSpec& spec) : spec(spec), pos(spec.pos), prevPos(spec.pos), velocity(spec.velocity) {}

    ProjectileSpec spec;
    glm::vec3 pos;
    glm::vec3 prevPos;
    glm::vec3 velocity;
    int age = 0;
    bool stuck = false;
    bool removed = false;

    // One 20-TPS tick. `mobs` may be null (tests / hurtsMobs == false).
    void tick(MobContext& ctx, std::vector<std::unique_ptr<Mob>>* mobs);

    // Unit direction the projectile is pointing (last travel direction).
    glm::vec3 direction() const;

  private:
    glm::vec3 lastDir{0.0f, 0.0f, 1.0f};
};
