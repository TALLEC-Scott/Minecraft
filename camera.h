#pragma once

#include <glm/glm.hpp>

#include "shader.h"

#define SPEED 0.001
#define GRAVITY 0.1
#define REACH 1.0f

class Camera {
public:
	Camera();
	
	void forward();
	void back();
	void left();
	void right();
	void up();
	void down();

	void speedUp();
	void resetSpeed();

	glm::vec3 getTargetPosition();

	void switchGravity();
	void fall();
	bool getG();

	void changeDirection(glm::vec3 direction);
	glm::vec3 getPosition() const;

	void defineLookAt(Shader shaderProgram);

    glm::vec3 getPosition();

private:
	bool gravity = false;
	glm::vec3 cameraPosition;
	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	float cameraSpeed;
};