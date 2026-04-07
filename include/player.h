#pragma once

#include "camera.h"
#include "cube.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <miniaudio.h>
#include <vector>
#include <string>

class World;
class ChunkManager;

class Player {
  public:
    Player();
    ~Player();

    void initAudio(ma_engine* engine);
    void destroyAudio();

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
    glm::ivec3 getPlacementPos() const { return placementPos; }

    // Hotbar
    static constexpr int HOTBAR_SIZE = 10;
    int getSelectedSlot() const { return selectedSlot; }
    void setSelectedSlot(int s) { selectedSlot = s; }
    block_type getSelectedBlockType() const { return hotbar[selectedSlot]; }
    const block_type* getHotbar() const { return hotbar; }

    bool isWalkMode() const { return camera.isWalkMode(); }
    float getPunchSwingAngle() const;
    glm::mat4 getArmModelMatrix() const;

    Camera& getCamera() { return camera; }

    void resetMouseState() { firstMouse = true; }
    void consumeMouseButtons() { leftClickHeld = true; rightClickHeld = true; }
    void setMouseSensitivity(float s) { mouseSensitivity = s; }

  private:
    Camera camera;

    // Block targeting
    glm::ivec3 targeted = glm::ivec3(0);
    glm::ivec3 placementPos = glm::ivec3(0);
    bool hasHighlight = false;

    // Hotbar
    block_type hotbar[HOTBAR_SIZE] = {AIR, GRASS, DIRT, STONE, WOOD, SAND, SNOW, GLOWSTONE, LEAVES, COAL_ORE};
    int selectedSlot = 0;
    bool rightClickHeld = false;

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

    // Footstep audio
    ma_engine* audioEngine = nullptr;
    struct StepSoundSet {
        std::vector<ma_sound> sounds;
    };
    static constexpr int NUM_SOUND_TYPES = 8; // STEP_GRASS..STEP_CLOTH
    StepSoundSet stepSounds[NUM_SOUND_TYPES];
    StepSoundSet breakSounds[NUM_SOUND_TYPES];
    bool stepSoundsLoaded = false;
    glm::vec3 lastStepPos = glm::vec3(0);
    float distSinceStep = 0.0f;
    block_type groundBlockType = AIR; // set by findGroundAndUpdate, reused by footstep code
    block_type feetBlockType = AIR;
    static constexpr float STEP_INTERVAL = 3.0f;
    void playStepSound(block_type groundBlock);
    void playBreakSound(block_type brokenBlock);
};
