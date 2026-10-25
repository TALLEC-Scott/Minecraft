#pragma once

#include "mob.h"

// Hide colors for the cow model. CreeperCow reuses the geometry with a
// green palette, so the colors are the only thing the two models differ in.
struct CowPalette {
    glm::vec3 hide, patch, muzzle, nostril, horn, udder, hoof, eyeWhite, pupil;
};

// Built model + the part indices pose() animates.
struct CowModelParts {
    MobModel model;
    int head, frontLeft, frontRight, backLeft, backRight;
};

CowModelParts buildCowModel(const CowPalette& palette);

// Passive quadruped, 10 HP. Wanders, looks at the player, panics when hurt.
// Spawns in groups of 2-4 on plains/forest (and tundra) grass. Model
// follows Minecraft's CowModel (16 px = 1 block), facing +Z, with extra
// detail boxes for the black/white patches, muzzle, horns and udder.
class Cow : public Mob {
  public:
    static constexpr float WANDER_SPEED = 0.08f; // blocks / tick (~1.6 b/s)
    static constexpr float PANIC_SPEED = 0.16f;

    Cow();

    const MobModel& model() const override;
    MobPose pose(float partialTick) const override;
    float eyeHeight() const override { return 1.3f; }

  protected:
    // For cow-shaped variants: same goals and pose, different registry row.
    explicit Cow(MobType variant);
};
