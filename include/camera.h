#pragma once

#include <glm/glm.hpp>

#include "shader.h"

#define SPEED 0.1f
#define GRAVITY 0.008f
#define TERMINAL_VELOCITY -0.8f
#define JUMP_VELOCITY 0.18f
#define REACH 8.0f
#define PLAYER_HEIGHT 1.62f

class Camera {
  public:
    Camera();

    void setDeltaTime(float dt) { deltaTime = dt; }

    void forward();
    void back();
    void left();
    void right();
    void up();
    void down();
    void jump();

    void speedUp();
    void resetSpeed();
    void setSpeed(float speed) { cameraSpeed = speed; }

    glm::vec3 getTargetPosition();

    void toggleWalkMode();
    bool isWalkMode() const { return walkMode; }
    bool isOnGround() const { return onGround; }
    // isSolid: callback to check if block at (x,y,z) is solid
    using BlockCheck = bool (*)(int, int, int, void*);
    using WaterCheck = bool (*)(int, int, int, void*);
    // getFlow: returns (fx, fy, fz) flow vector at block, zero if still
    using WaterFlowCheck = glm::vec3 (*)(int, int, int, void*);
    void update(BlockCheck isSolid, void* ctx = nullptr, WaterCheck isWater = nullptr,
                WaterFlowCheck getFlow = nullptr);
    bool isInWater() const { return inWater; }
    bool areEyesInWater() const { return eyesInWater; }

    void changeDirection(glm::vec3 direction);
    glm::vec3 getPosition() const;
    glm::vec3 getFront() const { return cameraFront; }
    void setPosition(glm::vec3 pos) { cameraPosition = pos; }
    void setWalkMode(bool wm) { walkMode = wm; }

    void defineLookAt(const Shader& shaderProgram);
    glm::mat4 getViewMatrix() const;

    // Camera shake (primed-TNT explosion). `magnitude` is the amplitude of
    // the per-frame sinusoidal jitter applied inside getViewMatrix(); `now`
    // drives the sine phase so the rattle animates. 0 disables.
    void setShake(float magnitude, double now) {
        shakeMagnitude = magnitude;
        shakeTime = now;
    }

  private:
    bool walkMode = false;
    bool onGround = false;
    bool inWater = false;
    bool eyesInWater = false;
    float velocityY = 0.0f;
    glm::vec3 pendingMove = glm::vec3(0); // accumulated walk movement, resolved in update()
    glm::vec3 cameraPosition;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float cameraSpeed;
    float deltaTime = 1.0f / 60.0f;
    float shakeMagnitude = 0.0f;
    double shakeTime = 0.0;
};
