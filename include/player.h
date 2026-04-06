#pragma once

#include "camera.h"
#include "cube.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class World;
class ChunkManager;

class Player {
  public:
    Player();

    // Input
    void handleInput(GLFWwindow* window, World* world);
    void updateMouseLook(double xPos, double yPos, int windowWidth, int windowHeight);

    // Physics & world interaction
    void update(World* world);

    // Queries for rendering
    glm::vec3 getPosition() const { return camera.getPosition(); }
    glm::vec3 getFront() const { return camera.getFront(); }
    glm::mat4 getViewMatrix() const { return camera.getViewMatrix(); }
    void defineLookAt(const Shader& shaderProgram) { camera.defineLookAt(shaderProgram); }

    bool hasTargetedBlock() const { return hasHighlight; }
    glm::ivec3 getTargetedBlock() const { return targeted; }

    bool isWalkMode() const { return camera.isWalkMode(); }
    float getPunchSwingAngle() const;
    glm::mat4 getArmModelMatrix() const;

    Camera& getCamera() { return camera; }

    void resetMouseState() { firstMouse = true; }
    void setMouseSensitivity(float s) { mouseSensitivity = s; }

  private:
    Camera camera;

    // Block targeting
    glm::ivec3 targeted = glm::ivec3(0);
    bool hasHighlight = false;

    // Punch animation
    bool isPunching = false;
    double punchStartTime = 0.0;
    bool leftClickHeld = false;
    static constexpr double PUNCH_DURATION = 0.3;

    // Double-tap space detection
    bool spaceWasPressed = false;
    double lastSpaceTap = 0.0;
    static constexpr double DOUBLE_TAP_TIME = 0.3;

    // Mouse look state
    float yaw = -90.0f;
    float pitch = 0.0f;
    bool firstMouse = true;
    float mouseSensitivity = 1.0f;

    void findGroundAndUpdate(World* world);
    void updateTargetedBlock(World* world);
};
