#include "camera.h"
#include <cmath>

Camera::Camera() {
	this->cameraPosition = glm::vec3(15.0f, 90.0f, 15.0f);
	this->cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	this->cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	this->cameraSpeed = SPEED;
}

void Camera::forward() {
	if (walkMode) {
		glm::vec3 flatFront = glm::normalize(glm::vec3(cameraFront.x, 0, cameraFront.z));
		pendingMove += cameraSpeed * flatFront;
	} else {
		cameraPosition += cameraSpeed * cameraFront;
	}
}

void Camera::back() {
	if (walkMode) {
		glm::vec3 flatFront = glm::normalize(glm::vec3(cameraFront.x, 0, cameraFront.z));
		pendingMove -= cameraSpeed * flatFront;
	} else {
		cameraPosition -= cameraSpeed * cameraFront;
	}
}

void Camera::up() {
	if (!walkMode) {
		cameraPosition += cameraSpeed * cameraUp;
	}
}

void Camera::down() {
	if (!walkMode) {
		cameraPosition -= cameraSpeed * cameraUp;
	}
}

void Camera::jump() {
	if (walkMode && onGround) {
		velocityY = JUMP_VELOCITY;
		onGround = false;
	}
}

void Camera::left() {
	if (walkMode)
		pendingMove -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	else
		cameraPosition -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
}

void Camera::right() {
	if (walkMode)
		pendingMove += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	else
		cameraPosition += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
}

void Camera::speedUp() {
	cameraSpeed = 3 * SPEED;
}

void Camera::resetSpeed() {
	cameraSpeed = SPEED;
}

void Camera::toggleWalkMode() {
	walkMode = !walkMode;
	if (walkMode) {
		velocityY = 0;
		onGround = false;
	}
}

void Camera::update(float groundHeight, BlockCheck isSolid, void* ctx) {
	if (!walkMode) { pendingMove = glm::vec3(0); return; }

	// Apply pending horizontal movement with collision
	if (glm::dot(pendingMove, pendingMove) > 0.000001f) {
		glm::vec3 newPos = cameraPosition + pendingMove;
		int nx = (int)std::floor(newPos.x);
		int nz = (int)std::floor(newPos.z);
		int feetY = (int)std::floor(cameraPosition.y - PLAYER_HEIGHT);
		int bodyY = feetY + 1;

		// Check if destination has solid blocks at feet or body level
		bool blocked = isSolid(nx, feetY, nz, ctx) || isSolid(nx, bodyY, nz, ctx);
		if (!blocked) {
			cameraPosition.x = newPos.x;
			cameraPosition.z = newPos.z;
		}
	}
	pendingMove = glm::vec3(0);

	// Apply gravity with terminal velocity
	velocityY -= GRAVITY;
	if (velocityY < TERMINAL_VELOCITY) velocityY = TERMINAL_VELOCITY;
	cameraPosition.y += velocityY;

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

void Camera::defineLookAt(Shader shaderProgram) {
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
