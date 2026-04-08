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

void Camera::update(BlockCheck isSolid, void* ctx, WaterCheck isWater) {
    if (!walkMode) {
        pendingMove = glm::vec3(0);
        return;
    }

    auto solidCheck = [&](int bx, int by, int bz) { return isSolid(bx, by, bz, ctx); };

    // Check if any part of player body is in water (feet, torso, or head)
    inWater = false;
    if (isWater) {
        int fx = (int)std::floor(cameraPosition.x);
        int fz = (int)std::floor(cameraPosition.z);
        int fy = (int)std::floor(cameraPosition.y - PLAYER_HEIGHT);
        int ty = fy + 1; // torso
        int hy = (int)std::floor(cameraPosition.y); // head/eyes
        inWater = isWater(fx, fy, fz, ctx) || isWater(fx, ty, fz, ctx) || isWater(fx, hy, fz, ctx);
    }

    glm::vec3 feetPos = {cameraPosition.x, cameraPosition.y - PLAYER_HEIGHT, cameraPosition.z};

    glm::vec3 move = pendingMove;
    if (inWater) {
        move *= 0.5f;
        // Apply vertical component of swim movement to velocity
        velocityY += move.y;
        move.y = 0;
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
    return glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);
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
