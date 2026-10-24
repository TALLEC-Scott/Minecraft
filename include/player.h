#pragma once

#include "camera.h"
#include "cube.h"
#include "player_health.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <miniaudio.h>
#include <vector>
#include <string>

class NetSession;

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
    void setHotbarSlot(int slot, block_type type) {
        if (slot >= 0 && slot < HOTBAR_SIZE) hotbar[slot] = type;
    }
    const block_type* getHotbar() const { return hotbar; }

    bool isWalkMode() const { return camera.isWalkMode(); }
    float getPunchSwingAngle() const;
    glm::mat4 getArmModelMatrix() const;

    // Block-placement "pop" effect. Progress 0→1 over PLACE_ANIM_DURATION.
    // Returns <0 when no animation is active.
    float getPlaceAnimProgress() const;
    glm::ivec3 getLastPlacedPos() const { return lastPlacedPos; }

    Camera& getCamera() { return camera; }

    // --- Health & combat ---
    static constexpr float ATTACK_REACH = 3.5f;     // mob melee reach (blocks from the eye)
    static constexpr double ATTACK_COOLDOWN = 0.25; // seconds between damaging hits
    static constexpr float FIST_DAMAGE = 1.0f;      // no tools yet; ×1.5 crit while falling
    static constexpr float HURT_KNOCKBACK = 8.0f;   // blocks / second, decays in Camera

    const PlayerHealth& getHealth() const { return health; }
    bool isDead() const { return health.isDead(); }
    glm::vec3 getFeetPosition() const { return camera.getPosition() - glm::vec3(0.0f, PLAYER_HEIGHT, 0.0f); }
    // Damage from a world position (mob, explosion). Knockback pushes away
    // from `fromPos` horizontally. Respects the invulnerability window.
    void hurt(float amount, glm::vec3 fromPos, bool knockback = true);
    // Full health, zero motion, camera (eye) at `eyePos`.
    void respawn(glm::vec3 eyePos);
    float getHurtFlash() const;

    // Multiplayer glue. When `net` is non-null, handleInput sends
    // intents through the session and skips the local world write iff
    // the session is currently suppressing (i.e. the client). Host and
    // offline play keep applying edits locally.
    void setNetSession(NetSession* net) { netSession = net; }

    void resetMouseState() { firstMouse = true; }
    void consumeMouseButtons() {
        leftClickHeld = true;
        rightClickHeld = true;
    }
    void setMouseSensitivity(float s) { mouseSensitivity = s; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    void setYawPitch(float y, float p) {
        yaw = y;
        pitch = p;
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        camera.changeDirection(dir);
    }

  private:
    Camera camera;

    // Block targeting
    glm::ivec3 targeted = glm::ivec3(0);
    glm::ivec3 placementPos = glm::ivec3(0);
    bool hasHighlight = false;

    // Hotbar
    block_type hotbar[HOTBAR_SIZE] = {AIR, GRASS, DIRT, STONE, WOOD, SAND, WATER, GLOWSTONE, LEAVES, SNOW};
    int selectedSlot = 0;
    bool rightClickHeld = false;

    // Punch animation
    bool isPunching = false;
    double punchStartTime = 0.0;
    bool leftClickHeld = false;
    static constexpr double PUNCH_DURATION = 0.3;

    // Block-placement pop animation
    glm::ivec3 lastPlacedPos = glm::ivec3(0);
    double placeAnimStart = -1.0;
    static constexpr double PLACE_ANIM_DURATION = 0.22;

    // Double-tap space detection
    bool spaceWasPressed = false;
    double lastSpaceTap = 0.0;
    static constexpr double DOUBLE_TAP_TIME = 0.3;

    // Double-tap W to sprint. Sprint stays active while W is held and
    // releases the moment the player lets go of forward.
    bool wWasPressed = false;
    double lastWTap = 0.0;
    bool sprintLatched = false;

    // Mouse look state
    float yaw = -90.0f;
    float pitch = 0.0f;
    bool firstMouse = true;
    float mouseSensitivity = 1.0f;

    void findGroundAndUpdate(World* world);
    void updateTargetedBlock(World* world);
    // Left-click melee: true if a mob was in reach (and in front of any
    // targeted block) — the click is then consumed and no block breaks.
    bool tryAttackMob(World* world);
    // Right-click mob interaction (shearing a woolly sheep): true if it
    // happened — the click is then consumed and no block is placed.
    bool tryInteractMob(World* world);
    void playHitSound();

    PlayerHealth health;
    double lastAttackTime = -1.0;

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

    // Underwater audio
    ma_sound underwaterAmbience{};
    ma_sound enterSounds[3]{};
    ma_sound exitSounds[3]{};
    ma_sound bubbleSounds[6]{};
    bool wasSubmerged = false;
    bool wasEyesUnder = false;
    double lastBubbleTime = 0.0;

    NetSession* netSession = nullptr;
};
