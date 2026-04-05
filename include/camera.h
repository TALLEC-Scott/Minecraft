#pragma once

#include <glm/glm.hpp>

#include "shader.h"

#define SPEED 0.15f
#define GRAVITY 0.008f
#define TERMINAL_VELOCITY -0.8f
#define JUMP_VELOCITY 0.18f
#define REACH 8.0f
#define PLAYER_HEIGHT 1.7f

class Camera {
  public:
    Camera();

    void forward();
    void back();
    void left();
    void right();
    void up();
    void down();
    void jump();

    void speedUp();
    void resetSpeed();

    glm::vec3 getTargetPosition();

    void toggleWalkMode();
    bool isWalkMode() const { return walkMode; }
    // isSolid: callback to check if block at (x,y,z) is solid
    using BlockCheck = bool (*)(int, int, int, void*);
    void update(float groundHeight, BlockCheck isSolid, void* ctx = nullptr);

    void changeDirection(glm::vec3 direction);
    glm::vec3 getPosition() const;
    glm::vec3 getFront() const { return cameraFront; }

    void defineLookAt(Shader shaderProgram);
    glm::mat4 getViewMatrix() const;

  private:
    bool walkMode = false;
    bool onGround = false;
    float velocityY = 0.0f;
    glm::vec3 pendingMove = glm::vec3(0); // accumulated walk movement, resolved in update()
    glm::vec3 cameraPosition;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float cameraSpeed;
};
