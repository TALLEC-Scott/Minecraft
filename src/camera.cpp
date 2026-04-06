#include "camera.h"
#include <cmath>

Camera::Camera() {
    this->cameraPosition = glm::vec3(15.0f, 90.0f, 15.0f);
    this->cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    this->cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    this->cameraSpeed = SPEED;
}

void Camera::forward() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode) {
        glm::vec3 flatFront = glm::normalize(glm::vec3(cameraFront.x, 0, cameraFront.z));
        pendingMove += s * flatFront;
    } else {
        cameraPosition += s * cameraFront;
    }
}

void Camera::back() {
    float s = cameraSpeed * deltaTime * 60.0f;
    if (walkMode) {
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
    if (walkMode && onGround) {
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

void Camera::update(float groundHeight, BlockCheck isSolid, void* ctx) {
    if (!walkMode) {
        pendingMove = glm::vec3(0);
        return;
    }

    // Apply pending horizontal movement with collision
    if (glm::dot(pendingMove, pendingMove) > 0.000001f) {
        glm::vec3 newPos = cameraPosition + pendingMove;
        int nx = (int)std::floor(newPos.x);
        int nz = (int)std::floor(newPos.z);
        int feetY = (int)std::floor(cameraPosition.y - PLAYER_HEIGHT);
        int bodyY = feetY + 1;

        bool blocked = isSolid(nx, feetY, nz, ctx) || isSolid(nx, bodyY, nz, ctx);
        if (!blocked) {
            cameraPosition.x = newPos.x;
            cameraPosition.z = newPos.z;
        }
    }
    pendingMove = glm::vec3(0);

    // Apply gravity with terminal velocity (also delta-time scaled)
    float dtScale = deltaTime * 60.0f;
    velocityY -= GRAVITY * dtScale;
    if (velocityY < TERMINAL_VELOCITY) velocityY = TERMINAL_VELOCITY;
    cameraPosition.y += velocityY * dtScale;

    // Ground collision
    float feetY = groundHeight + PLAYER_HEIGHT;
    if (cameraPosition.y <= feetY) {
        cameraPosition.y = feetY;
        velocityY = 0;
        onGround = true;
    } else {
        onGround = false;
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
