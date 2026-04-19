#include "player.h"
#include "particle_system.h"
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
        ma_sound_uninit(&underwaterAmbience);
        for (auto& s : enterSounds) ma_sound_uninit(&s);
        for (auto& s : exitSounds) ma_sound_uninit(&s);
        for (auto& s : bubbleSounds) ma_sound_uninit(&s);
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

    // Underwater sounds
    ma_sound_init_from_file(engine, "assets/Sounds/ambient/underwater/underwater_ambience.wav", MA_SOUND_FLAG_DECODE,
                            nullptr, nullptr, &underwaterAmbience);
    ma_sound_set_looping(&underwaterAmbience, MA_TRUE);
    ma_sound_set_volume(&underwaterAmbience, 0.4f);
    for (int i = 0; i < 3; i++) {
        std::string path = "assets/Sounds/ambient/underwater/enter" + std::to_string(i + 1) + ".wav";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &enterSounds[i]);
    }
    for (int i = 0; i < 3; i++) {
        std::string path = "assets/Sounds/ambient/underwater/exit" + std::to_string(i + 1) + ".wav";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &exitSounds[i]);
    }
    for (int i = 0; i < 6; i++) {
        std::string path = "assets/Sounds/ambient/underwater/bubbles" + std::to_string(i + 1) + ".wav";
        ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &bubbleSounds[i]);
        ma_sound_set_volume(&bubbleSounds[i], 0.3f);
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
    bool wDown = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    if (wDown) camera.forward();
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.back();
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.left();
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.right();

    // Double-tap W → latch sprint until the player releases forward.
    if (wDown && !wWasPressed) {
        double now = glfwGetTime();
        if (now - lastWTap < DOUBLE_TAP_TIME) sprintLatched = true;
        lastWTap = now;
    }
    if (!wDown) sprintLatched = false;
    wWasPressed = wDown;

    // Space: double-tap toggles walk/fly. Holding space keeps jumping on
    // land (camera.jump() gates on onGround, so it only fires when grounded)
    // and keeps pushing up in fly / water.
    {
        bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasPressed) {
            double now = glfwGetTime();
            if (now - lastSpaceTap < DOUBLE_TAP_TIME) {
                camera.toggleWalkMode();
                lastSpaceTap = 0;
            } else {
                lastSpaceTap = now;
            }
        }
        if (spaceDown) {
            if (camera.isWalkMode())
                camera.jump();  // onGround-gated, so auto-repeats on landing
            else
                camera.up();
        }
        spaceWasPressed = spaceDown;
    }

    // Down (fly mode only) — Shift or Q
    if (!camera.isWalkMode() &&
        (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS))
        camera.down();

    // Sprint — Shift in walk mode, R always, or double-tap W latch.
    bool sprint = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS || sprintLatched;
    if (camera.isWalkMode()) sprint = sprint || glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (sprint)
        camera.speedUp();
    else
        camera.resetSpeed();

    // Hotbar selection: keys 1-9 = slots 0-8, key 0 = slot 9
    for (int i = 0; i < HOTBAR_SIZE; i++) {
        int key = (i < 9) ? (GLFW_KEY_1 + i) : GLFW_KEY_0;
        if (glfwGetKey(window, key) == GLFW_PRESS) selectedSlot = i;
    }

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

    // Right click: ignite TNT if targeted, otherwise place block
    bool rightDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    if (rightDown && !rightClickHeld && world && hasHighlight) {
        Cube* targetBlock = world->getBlock(targeted.x, targeted.y, targeted.z);
        if (targetBlock && targetBlock->getType() == TNT) {
            world->igniteTnt(targeted, glfwGetTime());
            isPunching = true;
            punchStartTime = glfwGetTime();
        } else if (hotbar[selectedSlot] != AIR) {
            glm::vec3 camPos = camera.getPosition();
            int px = (int)std::floor(camPos.x);
            int pzCoord = (int)std::floor(camPos.z);
            int pyHead = (int)std::floor(camPos.y);
            int pyFeet = (int)std::floor(camPos.y - PLAYER_HEIGHT);
            // Don't place inside player body (2 blocks tall)
            bool blocked = (placementPos.x == px && placementPos.z == pzCoord &&
                            (placementPos.y == pyHead || placementPos.y == pyFeet));
            if (!blocked) {
                playBreakSound(hotbar[selectedSlot]);
                world->placeBlock(placementPos, hotbar[selectedSlot]);
                // Arm swing (same feedback as left-click) + outline pop at placement pos
                isPunching = true;
                punchStartTime = glfwGetTime();
                lastPlacedPos = placementPos;
                placeAnimStart = glfwGetTime();
            }
        }
    }
    rightClickHeld = rightDown;
}

void Player::updateMouseLook(double xPos, double yPos, int /*windowWidth*/, int /*windowHeight*/) {
    static double lastX = 0, lastY = 0;
    static bool initialized = false;
    if (firstMouse || !initialized) {
        lastX = xPos;
        lastY = yPos;
        firstMouse = false;
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

    // Movement sounds: swim sounds when any body part in water, footsteps on land
    bool inWater = camera.isInWater();
    bool eyesUnder = camera.areEyesInWater();
    bool moving = camera.isWalkMode() && (inWater || camera.isOnGround());
    if (stepSoundsLoaded && moving) {
        glm::vec3 pos = camera.getPosition();
        float dx = pos.x - lastStepPos.x;
        float dz = pos.z - lastStepPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > 0.001f) {
            distSinceStep += std::sqrt(distSq);
            lastStepPos = pos;
        }

        float interval = inWater ? STEP_INTERVAL * 3.0f : STEP_INTERVAL;
        if (distSinceStep >= interval) {
            distSinceStep = 0.0f;
            playStepSound(inWater ? WATER : groundBlockType);
        }
    }

    // Underwater audio: splash on enter/exit (any body part), bubbles+ambient only when eyes under
    static std::mt19937 waterRng(42);
    if (stepSoundsLoaded) {
        if (inWater && !wasSubmerged) {
            int idx = waterRng() % 3;
            ma_sound_seek_to_pcm_frame(&enterSounds[idx], 0);
            ma_sound_start(&enterSounds[idx]);
        } else if (!inWater && wasSubmerged) {
            int idx = waterRng() % 3;
            ma_sound_seek_to_pcm_frame(&exitSounds[idx], 0);
            ma_sound_start(&exitSounds[idx]);
        }
        if (eyesUnder && !wasEyesUnder) {
            ma_sound_seek_to_pcm_frame(&underwaterAmbience, 0);
            ma_sound_start(&underwaterAmbience);
        } else if (!eyesUnder && wasEyesUnder) {
            ma_sound_stop(&underwaterAmbience);
        }
        if (eyesUnder) {
            double now = glfwGetTime();
            if (now - lastBubbleTime > 3.0 + (std::fmod(now * 7.3, 4.0))) {
                lastBubbleTime = now;
                int idx = waterRng() % 6;
                ma_sound_seek_to_pcm_frame(&bubbleSounds[idx], 0);
                ma_sound_start(&bubbleSounds[idx]);
            }
        }
        // Splash bubbles only when first entering water — not a continuous
        // stream. Underwater motes are handled in the main render loop so
        // they benefit from camera front direction.
        if (inWater && !wasSubmerged && world && world->particles) {
            glm::vec3 chest = camera.getPosition() - glm::vec3(0.0f, PLAYER_HEIGHT * 0.5f, 0.0f);
            world->particles->spawnBubbles(chest, 18);
        }
        wasSubmerged = inWater;
        wasEyesUnder = eyesUnder;
    }

    // Block targeting
    updateTargetedBlock(world);
}

void Player::findGroundAndUpdate(World* world) {
    auto blockCheck = [](int bx, int by, int bz, void* ctx) -> bool {
        auto* cm = static_cast<ChunkManager*>(ctx);
        int cx = worldToChunk(bx);
        int cz = worldToChunk(bz);
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        block_type bt = chunk->getBlockType(worldToLocal(bx, cx), by, worldToLocal(bz, cz));
        return hasFlag(bt, BF_SOLID);
    };

    auto waterCheck = [](int bx, int by, int bz, void* ctx) -> bool {
        auto* cm = static_cast<ChunkManager*>(ctx);
        int cx = worldToChunk(bx);
        int cz = worldToChunk(bz);
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        return chunk->getBlockType(worldToLocal(bx, cx), by, worldToLocal(bz, cz)) == WATER;
    };

    // Flow vector at a water block: (fx, fy, fz). Horizontal from level
    // gradient; Y = -1 for falling water (pushes player downward).
    auto flowCheck = [](int bx, int by, int bz, void* ctx) -> glm::vec3 {
        constexpr float FLOW_INTO_AIR_WEIGHT = 8.0f;
        auto* cm = static_cast<ChunkManager*>(ctx);
        // Sample one cell: returns {blockType, waterRaw}. Chunk lookup is
        // cached across calls — player + 4 neighbors usually share a chunk.
        int lastCx = INT_MIN, lastCz = INT_MIN;
        Chunk* lastChunk = nullptr;
        auto sample = [&](int x, int y, int z) -> std::pair<block_type, uint8_t> {
            if (y < 0 || y >= CHUNK_HEIGHT) return {STONE, 0};
            int cx = worldToChunk(x), cz = worldToChunk(z);
            if (cx != lastCx || cz != lastCz) {
                lastCx = cx;
                lastCz = cz;
                lastChunk = cm->getChunk(cx, cz);
            }
            if (!lastChunk) return {STONE, 0};
            int lx = worldToLocal(x, cx), lz = worldToLocal(z, cz);
            block_type t = lastChunk->getBlockType(lx, y, lz);
            return {t, t == WATER ? lastChunk->getWaterLevel(lx, y, lz) : (uint8_t)0};
        };
        auto self = sample(bx, by, bz);
        if (self.first != WATER) return glm::vec3(0);
        if (waterIsFalling(self.second)) return glm::vec3(0, -1, 0);
        int myLvl = waterIsSource(self.second) ? 0 : waterFlowLevel(self.second);
        glm::vec3 flow(0);
        static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : dirs) {
            auto n = sample(bx + d[0], by, bz + d[1]);
            float contrib = 0;
            if (n.first == AIR) {
                contrib = FLOW_INTO_AIR_WEIGHT;
            } else if (n.first == WATER) {
                int nLvl = waterIsSource(n.second) ? 0 : waterFlowLevel(n.second);
                contrib = (float)(nLvl - myLvl);
            }
            flow.x += d[0] * contrib;
            flow.z += d[1] * contrib;
        }
        return flow;
    };

    camera.update(blockCheck, world->chunkManager, waterCheck, flowCheck);
    if (!camera.isWalkMode()) return;

    // Footstep sounds: look up block types at player feet. Block coords are
    // centered on integers (block N spans [N-0.5, N+0.5]), so we add +0.5
    // before flooring — same convention as camera.cpp's collision checks.
    // Without this shift, standing on a 1-block-thick surface reads the
    // block below (e.g. sand on dirt → wrong step sound).
    glm::vec3 pos = camera.getPosition();
    int bx = (int)std::floor(pos.x + 0.5f);
    int bz = (int)std::floor(pos.z + 0.5f);
    int cx = worldToChunk(bx);
    int cz = worldToChunk(bz);
    Chunk* chunk = world->chunkManager->getChunk(cx, cz);
    groundBlockType = AIR;
    feetBlockType = AIR;
    if (chunk) {
        int feetY = (int)std::floor(pos.y - PLAYER_HEIGHT + 0.5f);
        Cube* gb = chunk->getBlock(worldToLocal(bx, cx), feetY - 1, worldToLocal(bz, cz));
        if (gb) groundBlockType = gb->getType();
        Cube* fb = chunk->getBlock(worldToLocal(bx, cx), feetY, worldToLocal(bz, cz));
        if (fb) feetBlockType = fb->getType();
    }
}

void Player::updateTargetedBlock(World* world) {
    glm::vec3 pos = camera.getPosition();
    glm::vec3 front = camera.getFront();
    hasHighlight = world->raycast(pos, front, REACH, targeted, placementPos);
}

float Player::getPunchSwingAngle() const {
    if (!isPunching) return 0.0f;
    float t = (float)((glfwGetTime() - punchStartTime) / PUNCH_DURATION);
    return std::sin(t * glm::radians(180.0f)) * -60.0f;
}

float Player::getPlaceAnimProgress() const {
    if (placeAnimStart < 0) return -1.0f;
    double t = (glfwGetTime() - placeAnimStart) / PLACE_ANIM_DURATION;
    if (t >= 1.0) return -1.0f; // done
    return (float)t;
}

glm::mat4 Player::getArmModelMatrix() const {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(0.4f, -0.4f, -0.6f));
    m = glm::rotate(m, glm::radians(getPunchSwingAngle()), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(-10.0f), glm::vec3(0, 0, 1));
    return m;
}
