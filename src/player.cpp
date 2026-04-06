#include "player.h"
#include "world.h"
#include "ChunkManager.h"
#include <cmath>
#include <random>

Player::Player() {}

Player::~Player() = default;

void Player::destroyAudio() {
    if (stepSoundsLoaded) {
        for (auto& set : stepSounds)
            for (auto& s : set.sounds) ma_sound_uninit(&s);
        for (auto& set : breakSounds)
            for (auto& s : set.sounds) ma_sound_uninit(&s);

        stepSoundsLoaded = false;
    }
}

void Player::initAudio(ma_engine* engine) {
    audioEngine = engine;
    if (!engine) return;

    struct SoundDef {
        StepSound type;
        const char* prefix;
        int count;
    };
    SoundDef defs[] = {
        {STEP_GRASS, "assets/Sounds/step/grass/grass", 6}, {STEP_STONE, "assets/Sounds/step/stone/stone", 6},
        {STEP_SAND, "assets/Sounds/step/sand/sand", 5},    {STEP_GRAVEL, "assets/Sounds/step/gravel/gravel", 4},
        {STEP_SNOW, "assets/Sounds/step/snow/snow", 4},    {STEP_WOOD, "assets/Sounds/step/wood/wood", 6},
        {STEP_WATER, "assets/Sounds/step/water/swim", 4},  {STEP_CLOTH, "assets/Sounds/dig/cloth/cloth", 4},
    };

    for (auto& def : defs) {
        auto& set = stepSounds[def.type - 1];
        set.sounds.resize(def.count);
        for (int i = 0; i < def.count; i++) {
            std::string path = std::string(def.prefix) + std::to_string(i + 1) + ".wav";
            ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &set.sounds[i]);
        }
    }
    // Load break/dig sounds (same categories, different files)
    SoundDef breakDefs[] = {
        {STEP_GRASS, "assets/Sounds/dig/grass/grass", 4}, {STEP_STONE, "assets/Sounds/dig/stone/stone", 4},
        {STEP_SAND, "assets/Sounds/dig/sand/sand", 4},    {STEP_GRAVEL, "assets/Sounds/dig/gravel/gravel", 4},
        {STEP_SNOW, "assets/Sounds/dig/snow/snow", 4},    {STEP_WOOD, "assets/Sounds/dig/wood/wood", 4},
        {STEP_CLOTH, "assets/Sounds/dig/cloth/cloth", 4},
    };
    for (auto& def : breakDefs) {
        auto& set = breakSounds[def.type - 1];
        set.sounds.resize(def.count);
        for (int i = 0; i < def.count; i++) {
            std::string path = std::string(def.prefix) + std::to_string(i + 1) + ".wav";
            ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &set.sounds[i]);
        }
    }

    stepSoundsLoaded = true;
    lastStepPos = camera.getPosition();
}

void Player::playStepSound(block_type groundBlock) {
    StepSound ss = getStepSound(groundBlock);
    if (ss == STEP_NONE || !stepSoundsLoaded) return;

    auto& set = stepSounds[ss - 1];
    if (set.sounds.empty()) return;

    static std::mt19937 rng(42);
    int idx = std::uniform_int_distribution<int>(0, (int)set.sounds.size() - 1)(rng);
    ma_sound_seek_to_pcm_frame(&set.sounds[idx], 0);
    ma_sound_start(&set.sounds[idx]);
}

void Player::playBreakSound(block_type brokenBlock) {
    StepSound ss = getStepSound(brokenBlock);
    if (ss == STEP_NONE || ss == STEP_WATER || !stepSoundsLoaded) return;
    if (ss - 1 >= NUM_SOUND_TYPES) return;

    auto& set = breakSounds[ss - 1];
    if (set.sounds.empty()) return;

    static std::mt19937 rng(123);
    int idx = std::uniform_int_distribution<int>(0, (int)set.sounds.size() - 1)(rng);
    ma_sound_seek_to_pcm_frame(&set.sounds[idx], 0);
    ma_sound_start(&set.sounds[idx]);
}

void Player::handleInput(GLFWwindow* window, World* world) {
    // Movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.forward();
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.back();
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.left();
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.right();

    // Space: double-tap toggles walk/fly, single = jump or fly up
    {
        bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasPressed) {
            double now = glfwGetTime();
            if (now - lastSpaceTap < DOUBLE_TAP_TIME) {
                camera.toggleWalkMode();
                lastSpaceTap = 0;
            } else {
                lastSpaceTap = now;
                if (camera.isWalkMode())
                    camera.jump();
                else
                    camera.up();
            }
        } else if (spaceDown && !camera.isWalkMode()) {
            camera.up();
        }
        spaceWasPressed = spaceDown;
    }

    // Down (fly mode only)
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.down();

    // Sprint
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.speedUp();
    else
        camera.resetSpeed();

    // Left click: punch + break block
    bool leftDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    if (leftDown && !leftClickHeld && world) {
        isPunching = true;
        punchStartTime = glfwGetTime();
        if (hasHighlight) {
            Cube* block = world->getBlock(targeted.x, targeted.y, targeted.z);
            if (block) playBreakSound(block->getType());
            world->destroyBlock(glm::vec3(targeted));
        }
    }
    leftClickHeld = leftDown;
}

void Player::updateMouseLook(double xPos, double yPos, int /*windowWidth*/, int /*windowHeight*/) {
    if (firstMouse) {
        firstMouse = false;
        return;
    }

    static double lastX = 0, lastY = 0;
    static bool initialized = false;
    if (!initialized) {
        lastX = xPos;
        lastY = yPos;
        initialized = true;
        return;
    }

    float sensitivity = 0.1f * mouseSensitivity;
    float xOffset = (float)(xPos - lastX) * sensitivity;
    float yOffset = (float)(lastY - yPos) * sensitivity;
    lastX = xPos;
    lastY = yPos;

    yaw += xOffset;
    pitch += yOffset;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camera.changeDirection(direction);
}

void Player::update(World* world) {
    // Update punch animation
    if (isPunching && (glfwGetTime() - punchStartTime) > PUNCH_DURATION) isPunching = false;

    // Physics
    findGroundAndUpdate(world);

    // Footstep sounds — reuses groundBlockType/feetBlockType from findGroundAndUpdate
    bool inWater = feetBlockType == WATER;
    if (stepSoundsLoaded && (inWater || (camera.isWalkMode() && camera.isOnGround()))) {
        glm::vec3 pos = camera.getPosition();
        float dx = pos.x - lastStepPos.x;
        float dz = pos.z - lastStepPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > 0.001f) {
            distSinceStep += std::sqrt(distSq);
            lastStepPos = pos;
        }

        if (distSinceStep >= STEP_INTERVAL) {
            distSinceStep = 0.0f;
            playStepSound(inWater ? WATER : groundBlockType);
        }
    }

    // Block targeting
    updateTargetedBlock(world);
}

void Player::findGroundAndUpdate(World* world) {
    if (!camera.isWalkMode()) return;

    glm::vec3 pos = camera.getPosition();
    int bx = (int)std::floor(pos.x);
    int bz = (int)std::floor(pos.z);

    // Block check callback
    auto blockCheck = [](int bx, int by, int bz, void* ctx) -> bool {
        auto* cm = static_cast<ChunkManager*>(ctx);
        int cx = worldToChunk(bx);
        int cz = worldToChunk(bz);
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        Cube* b = chunk->getBlock(worldToLocal(bx, cx), by, worldToLocal(bz, cz));
        return b && hasFlag(b->getType(), BF_SOLID);
    };

    // Find ground: pre-compute chunk, scan column
    int cx = worldToChunk(bx);
    int cz = worldToChunk(bz);
    int lx = worldToLocal(bx, cx);
    int lz = worldToLocal(bz, cz);
    Chunk* chunk = world->chunkManager->getChunk(cx, cz);
    float groundY = 0;
    groundBlockType = AIR;
    feetBlockType = AIR;
    if (chunk) {
        for (int y = std::min((int)pos.y + 1, CHUNK_HEIGHT - 1); y >= 0; y--) {
            Cube* b = chunk->getBlock(lx, y, lz);
            if (b && hasFlag(b->getType(), BF_SOLID)) {
                groundY = (float)(y + 1);
                groundBlockType = b->getType();
                break;
            }
        }
        // Check block at feet level for water
        int feetY = (int)std::floor(pos.y - PLAYER_HEIGHT);
        Cube* fb = chunk->getBlock(lx, feetY, lz);
        if (fb) feetBlockType = fb->getType();
    }

    camera.update(groundY, blockCheck, world->chunkManager);
}

void Player::updateTargetedBlock(World* world) {
    glm::vec3 pos = camera.getPosition();
    glm::vec3 front = camera.getFront();
    hasHighlight = world->raycast(pos, front, REACH, targeted);
}

float Player::getPunchSwingAngle() const {
    if (!isPunching) return 0.0f;
    float t = (float)((glfwGetTime() - punchStartTime) / PUNCH_DURATION);
    return std::sin(t * 3.14159f) * -60.0f;
}

glm::mat4 Player::getArmModelMatrix() const {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(0.4f, -0.4f, -0.6f));
    m = glm::rotate(m, glm::radians(getPunchSwingAngle()), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-10.0f), glm::vec3(0, 0, 1));
    return m;
}
