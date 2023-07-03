#include "camera.h"

GLfloat g = GRAVITY;

Camera::Camera() {
	this->cameraPosition = glm::vec3(15.0f, 17.0f, 15.0f);
	this->cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	this->cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	this->cameraSpeed = SPEED;
}

void Camera::forward() {
	cameraPosition += cameraSpeed * cameraFront;
}

void Camera::back() {
	cameraPosition -= cameraSpeed * cameraFront;
}

void Camera::up() {
	cameraPosition += cameraSpeed * cameraUp;
}

void Camera::down() {
	cameraPosition -= cameraSpeed * cameraUp;
}

void Camera::left() {
	cameraPosition -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
}

void Camera::right() {
	cameraPosition += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
}

void Camera::speedUp() {
	cameraSpeed = 500 * SPEED;
}

void Camera::resetSpeed() {
	cameraSpeed = SPEED;
}

void Camera::switchGravity() {
	this->gravity = !this->gravity;
}

void Camera::fall() {
	if (gravity) {
		cameraPosition.y -= g;
		g += GRAVITY;
	}
	else {
		g = GRAVITY;
	}
}

bool Camera::getG() {
	return gravity;
}

void Camera::changeDirection(glm::vec3 direction) {
	cameraFront = glm::normalize(direction);
}

void Camera::defineLookAt(Shader shaderProgram) {
	glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);
	shaderProgram.setMat4("view", view);
}

glm::vec3 Camera::getPosition() const {
	return cameraPosition;
}

glm::vec3 Camera::getTargetPosition() {
	glm::vec3 aimedBlock = cameraFront * REACH;
	glm::vec3 targetPosition = cameraPosition + aimedBlock;
	return targetPosition;
}

glm::vec3 Camera::getPosition() {
	return cameraPosition;
}
