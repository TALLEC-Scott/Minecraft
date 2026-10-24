#pragma once

// Data-driven box models for mobs. A MobModel is a flat list of boxes
// arranged in a parent/child hierarchy (body → head → snout, …). Each
// frame the mob produces a MobPose — one rotation (+ optional offset) per
// part — and MobRenderer composes the matrices. GL-free so models and
// pose math can be unit-tested.
//
// Coordinate conventions (model space, 1 unit = 1 block, 16 px = 1 unit):
//   - origin at the mob's feet center, +Y up, model FACES +Z
//   - a root part's `pivot` is relative to the feet; a child's pivot is
//     relative to its parent's pivot (NOT the parent's box center)
//   - `center` is the box center relative to its own pivot, so a leg
//     hanging from a hip pivot has center = (0, -legLen/2, 0)
//   - rotations are radians, applied Z·Y·X about the pivot (Minecraft
//     ModelPart order). Right-handed: a +X rotation swings the bottom of
//     a hanging part BACKWARD (toward -Z) and tips a face (+Z) DOWN. So a
//     zombie's arms held forward are rotation.x = -pi/2, and looking up is
//     a negative X rotation (poseHeadLook handles the sign).
//   - +Y rotation turns the part's front (+Z) toward +X.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

inline constexpr int MAX_MOB_PARTS = 32;

struct MobPart {
    const char* name;
    int parent = -1;             // index of parent part, -1 = attached to the body root
    glm::vec3 pivot{0.0f};       // joint position in parent space
    glm::vec3 center{0.0f};      // box center relative to the pivot
    glm::vec3 halfSize{0.5f};    // box half-extents
    glm::vec3 color{1.0f};       // tint (multiplies the texture layer)
    int textureLayer = -1;       // texture-array layer; -1 = plain white skin layer
    glm::vec3 restRotation{0.0f}; // baked rotation (e.g. a horizontal quadruped body)
};

class MobModel {
  public:
    std::vector<MobPart> parts;

    // Append a part, returning its index (use as `parent` for children).
    int add(const MobPart& part);
    // Index of the first part named `name`, or -1.
    int indexOf(std::string_view name) const;
    int size() const { return static_cast<int>(parts.size()); }
};

// Per-frame animation state. Index-aligned with MobModel::parts.
struct MobPose {
    std::array<glm::vec3, MAX_MOB_PARTS> rotation{}; // radians, added to restRotation
    std::array<glm::vec3, MAX_MOB_PARTS> offset{};   // extra pivot translation (parent space)
    std::array<bool, MAX_MOB_PARTS> hidden{};
    glm::vec3 scale{1.0f};    // whole-model scale about the feet (creeper swell, baby mobs)
    float whiteFlash = 0.0f;  // 0..1 blend toward white (creeper fuse)
};

// --- Common pose helpers ---------------------------------------------------

// Turn `head` by the head-vs-body yaw delta (degrees, camera convention:
// +yaw turns from +X toward +Z) and pitch (degrees, + = look up).
void poseHeadLook(MobPose& pose, int head, float relYawDeg, float pitchDeg);

// Classic quadruped gait: diagonal legs move together.
// `animPos` / `animSpeed` come from Mob::walkAnim(partialTick).
void poseQuadrupedLegs(MobPose& pose, int frontLeft, int frontRight, int backLeft, int backRight, float animPos,
                       float animSpeed);

// Biped legs (zombie, skeleton, creeper-style 4-leg also works by passing
// the pairs separately). Left/right swing in opposite phase.
void poseBipedLegs(MobPose& pose, int leftLeg, int rightLeg, float animPos, float animSpeed);

// Biped arms swinging opposite to the legs (passive humanoids).
void poseBipedArms(MobPose& pose, int leftArm, int rightArm, float animPos, float animSpeed);
