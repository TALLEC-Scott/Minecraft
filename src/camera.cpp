#include "camera.h"
#include "collision.h"
#include <cmath>

// PLAYER_TOTAL_HEIGHT and PLAYER_HALF_WIDTH defined in collision.h

Camera::Camera() {
    this->cameraPosition = glm::vec3(15.0f, 90.0f, 15.0f);
    this->cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    this->cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    this->cameraSpeed = SPEED;
}

void Camera::forward() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode && inWater) {
        // Swim in look direction (3D movement)
        pendingMove += s * cameraFront;
    } else if (walkMode) {
        glm::vec3 flatFront = glm::normalize(glm::vec3(cameraFront.x, 0, cameraFront.z));
        pendingMove += s * flatFront;
    } else {
        cameraPosition += s * cameraFront;
    }
}

void Camera::back() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode && inWater) {
        pendingMove -= s * cameraFront;
    } else if (walkMode) {
        glm::vec3 flatFront = glm::normalize(glm::vec3(cameraFront.x, 0, cameraFront.z));
        pendingMove -= s * flatFront;
    } else {
        cameraPosition -= s * cameraFront;
    }
}

void Camera::up() {
    if (!walkMode) {
        cameraPosition += cameraSpeed * deltaTime * 60.0f * cameraUp;
    }
}

void Camera::down() {
    if (!walkMode) {
        cameraPosition -= cameraSpeed * deltaTime * 60.0f * cameraUp;
    }
}

void Camera::jump() {
    if (walkMode && inWater) {
        velocityY += 0.04f * deltaTime * 25.0f;
    } else if (walkMode && onGround) {
        velocityY = JUMP_VELOCITY;
        onGround = false;
    }
}

void Camera::left() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode)
        pendingMove -= glm::normalize(glm::cross(cameraFront, cameraUp)) * s;
    else
        cameraPosition -= glm::normalize(glm::cross(cameraFront, cameraUp)) * s;
}

void Camera::right() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode)
        pendingMove += glm::normalize(glm::cross(cameraFront, cameraUp)) * s;
    else
        cameraPosition += glm::normalize(glm::cross(cameraFront, cameraUp)) * s;
}

void Camera::speedUp() {
    cameraSpeed = walkMode ? 1.5f * SPEED : 9 * SPEED;
}

void Camera::resetSpeed() {
    cameraSpeed = walkMode ? SPEED : 4.5f * SPEED;
}

void Camera::toggleWalkMode() {
    walkMode = !walkMode;
    if (walkMode) {
        velocityY = 0;
        onGround = false;
    }
}

void Camera::update(BlockCheck isSolid, void* ctx, WaterCheck isWater, WaterFlowCheck getFlow) {
    // Check water state always (needed for visual effects even in fly mode)
    inWater = false;
    eyesInWater = false;
    if (isWater) {
        // +0.5 shift for block-centered coordinates (block N spans N-0.5 to N+0.5)
        int fx = (int)std::floor(cameraPosition.x + 0.5f);
        int fz = (int)std::floor(cameraPosition.z + 0.5f);
        int fy = (int)std::floor(cameraPosition.y - PLAYER_HEIGHT + 0.5f);
        int ty = fy + 1;
        int hy = (int)std::floor(cameraPosition.y + 0.5f);
        eyesInWater = isWater(fx, hy, fz, ctx);
        inWater = isWater(fx, fy, fz, ctx) || isWater(fx, ty, fz, ctx) || eyesInWater;
    }

    if (!walkMode) {
        pendingMove = glm::vec3(0);
        return;
    }

    auto solidCheck = [&](int bx, int by, int bz) { return isSolid(bx, by, bz, ctx); };

    glm::vec3 feetPos = {cameraPosition.x, cameraPosition.y - PLAYER_HEIGHT, cameraPosition.z};

    glm::vec3 move = pendingMove;
    if (inWater) {
        move *= 0.5f;
        // Apply vertical component of swim movement to velocity (clamped)
        velocityY += move.y * 0.3f;
        velocityY = glm::clamp(velocityY, -0.15f, 0.15f);
        move.y = 0;
        // Water current: push player horizontally along stream; falling
        // water (flow.y < 0) pushes vertically down.
        if (getFlow) {
            constexpr float WATER_CURRENT_PUSH = 0.025f; // blocks/frame at 60fps
            constexpr float WATERFALL_DOWN_PUSH = 0.014f;
            int fx = (int)std::floor(cameraPosition.x + 0.5f);
            int fy = (int)std::floor(cameraPosition.y - PLAYER_HEIGHT + 0.5f);
            int fz = (int)std::floor(cameraPosition.z + 0.5f);
            glm::vec3 flow = getFlow(fx, fy, fz, ctx);
            glm::vec2 horiz(flow.x, flow.z);
            if (glm::dot(horiz, horiz) > 0.000001f) {
                horiz = glm::normalize(horiz);
                float push = WATER_CURRENT_PUSH * deltaTime * 60.0f;
                move.x += horiz.x * push;
                move.z += horiz.y * push;
            }
            if (flow.y < -0.001f) {
                velocityY -= WATERFALL_DOWN_PUSH * deltaTime * 60.0f;
            }
        }
    }

    // Resolve horizontal movement with wall sliding
    if (glm::dot(move, move) > 0.000001f) {
        feetPos = resolveMovement(feetPos, glm::vec3(move.x, 0, move.z), solidCheck);
        cameraPosition.x = feetPos.x;
        cameraPosition.z = feetPos.z;
    }
    pendingMove = glm::vec3(0);

    float dtScale = deltaTime * 60.0f;

    if (inWater) {
        // Water physics: drag is asymmetric — stronger when going up, weaker when falling
        // This preserves fall momentum but prevents dolphin-jumping out
        float drag = (velocityY > 0) ? 0.05f : 0.4f; // per-second retention: up=5%, down=40%
        velocityY *= std::pow(drag, deltaTime);
        velocityY -= GRAVITY * 0.25f * dtScale;
        float waterTerminal = TERMINAL_VELOCITY * 0.15f;
        if (velocityY < waterTerminal) velocityY = waterTerminal;
    } else {
        // Normal gravity
        velocityY -= GRAVITY * dtScale;
        if (velocityY < TERMINAL_VELOCITY) velocityY = TERMINAL_VELOCITY;
    }
    float moveY = velocityY * dtScale;

    // Resolve vertical movement with AABB (ground + ceiling)
    feetPos = {cameraPosition.x, cameraPosition.y - PLAYER_HEIGHT, cameraPosition.z};
    auto vResult = resolveVertical(feetPos, moveY, solidCheck);
    cameraPosition.y = vResult.newFeetY + PLAYER_HEIGHT;

    if (vResult.hitGround) {
        velocityY = 0;
        onGround = true;
    } else {
        onGround = false;
    }
    if (vResult.hitCeiling) {
        velocityY = 0;
    }
}

void Camera::changeDirection(glm::vec3 direction) {
    cameraFront = glm::normalize(direction);
}

glm::mat4 Camera::getViewMatrix() const {
    glm::vec3 eye = cameraPosition;
    if (shakeMagnitude > 0.0f) {
        // Three uncorrelated frequencies so the rattle doesn't fall into a
        // clean oscillation pattern, and — importantly — all three world
        // axes are perturbed so the felt intensity is the same regardless
        // of where the camera is facing. (X+Y only would collapse into a
        // depth wobble when yawed along ±X.) Magnitude decays externally
        // via World::update.
        float t = static_cast<float>(shakeTime);
        eye.x += std::sin(t * 83.0f) * shakeMagnitude * 0.25f;
        eye.y += std::cos(t * 67.0f) * shakeMagnitude * 0.25f;
        eye.z += std::sin(t * 79.0f) * shakeMagnitude * 0.25f;
    }
    return glm::lookAt(eye, eye + cameraFront, cameraUp);
}

void Camera::defineLookAt(const Shader& shaderProgram) {
    shaderProgram.setMat4("view", getViewMatrix());
}

glm::vec3 Camera::getPosition() const {
    return cameraPosition;
}

glm::vec3 Camera::getTargetPosition() {
    glm::vec3 aimedBlock = cameraFront * REACH;
    glm::vec3 targetPosition = cameraPosition + aimedBlock;
    return targetPosition;
}
