#include "player.h"
#include "world.h"
#include "ChunkManager.h"
#include <cmath>

Player::Player() {}

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
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.speedUp();
    else
        camera.resetSpeed();

    // Left click: punch + break block
    bool leftDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    if (leftDown && !leftClickHeld && world) {
        isPunching = true;
        punchStartTime = glfwGetTime();
        if (hasHighlight)
            world->destroyBlock(glm::vec3(targeted));
    }
    leftClickHeld = leftDown;
}

void Player::updateMouseLook(double xPos, double yPos, int windowWidth, int windowHeight) {
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

    float sensitivity = 0.1f;
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
    if (isPunching && (glfwGetTime() - punchStartTime) > PUNCH_DURATION)
        isPunching = false;

    // Physics
    findGroundAndUpdate(world);

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
        int cx = (bx >= 0) ? bx / CHUNK_SIZE : (bx - CHUNK_SIZE + 1) / CHUNK_SIZE;
        int cz = (bz >= 0) ? bz / CHUNK_SIZE : (bz - CHUNK_SIZE + 1) / CHUNK_SIZE;
        Chunk* chunk = cm->getChunk(cx, cz);
        if (!chunk || by < 0 || by >= CHUNK_HEIGHT) return false;
        Cube* b = chunk->getBlock(bx - cx * CHUNK_SIZE, by, bz - cz * CHUNK_SIZE);
        return b && b->getType() != AIR && b->getType() != WATER;
    };

    // Find ground: pre-compute chunk, scan column
    int cx = (bx >= 0) ? bx / CHUNK_SIZE : (bx - CHUNK_SIZE + 1) / CHUNK_SIZE;
    int cz = (bz >= 0) ? bz / CHUNK_SIZE : (bz - CHUNK_SIZE + 1) / CHUNK_SIZE;
    int lx = bx - cx * CHUNK_SIZE;
    int lz = bz - cz * CHUNK_SIZE;
    Chunk* chunk = world->chunkManager->getChunk(cx, cz);
    float groundY = 0;
    if (chunk) {
        for (int y = std::min((int)pos.y + 1, CHUNK_HEIGHT - 1); y >= 0; y--) {
            Cube* b = chunk->getBlock(lx, y, lz);
            if (b && b->getType() != AIR && b->getType() != WATER) {
                groundY = (float)(y + 1);
                break;
            }
        }
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
