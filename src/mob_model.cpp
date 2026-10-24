#include "mob_model.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

int MobModel::add(const MobPart& part) {
    parts.push_back(part);
    return static_cast<int>(parts.size()) - 1;
}

int MobModel::indexOf(std::string_view name) const {
    for (size_t i = 0; i < parts.size(); ++i)
        if (name == parts[i].name) return static_cast<int>(i);
    return -1;
}

namespace {
bool valid(int idx) {
    return idx >= 0 && idx < MAX_MOB_PARTS;
}
// Minecraft's limb formula: cos(pos * 0.6662) * 1.4 * amount (radians).
constexpr float LIMB_FREQ = 0.6662f;
constexpr float LIMB_AMPLITUDE = 1.4f;
} // namespace

void poseHeadLook(MobPose& pose, int head, float relYawDeg, float pitchDeg) {
    if (!valid(head)) return;
    // Body yaw increases from +X toward +Z, which is a NEGATIVE rotation
    // about +Y in the model's right-handed frame; looking up tips the
    // face (+Z) toward +Y, which is a negative X rotation.
    pose.rotation[head].y += glm::radians(-relYawDeg);
    pose.rotation[head].x += glm::radians(-pitchDeg);
}

void poseQuadrupedLegs(MobPose& pose, int frontLeft, int frontRight, int backLeft, int backRight, float animPos,
                       float animSpeed) {
    float a = std::cos(animPos * LIMB_FREQ) * LIMB_AMPLITUDE * animSpeed;
    float b = std::cos(animPos * LIMB_FREQ + glm::pi<float>()) * LIMB_AMPLITUDE * animSpeed;
    if (valid(backRight)) pose.rotation[backRight].x += a;
    if (valid(frontLeft)) pose.rotation[frontLeft].x += a;
    if (valid(backLeft)) pose.rotation[backLeft].x += b;
    if (valid(frontRight)) pose.rotation[frontRight].x += b;
}

void poseBipedLegs(MobPose& pose, int leftLeg, int rightLeg, float animPos, float animSpeed) {
    float a = std::cos(animPos * LIMB_FREQ) * LIMB_AMPLITUDE * animSpeed;
    if (valid(rightLeg)) pose.rotation[rightLeg].x += a;
    if (valid(leftLeg)) pose.rotation[leftLeg].x -= a;
}

void poseBipedArms(MobPose& pose, int leftArm, int rightArm, float animPos, float animSpeed) {
    // Arms swing at half the leg amplitude, opposite to the same-side leg.
    float a = std::cos(animPos * LIMB_FREQ) * LIMB_AMPLITUDE * 0.5f * animSpeed;
    if (valid(rightArm)) pose.rotation[rightArm].x -= a;
    if (valid(leftArm)) pose.rotation[leftArm].x += a;
}
