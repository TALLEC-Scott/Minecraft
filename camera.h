#pragma once

#include <glm/glm.hpp>

#include "shader.h"

#define SPEED 0.001
#define GRAVITY 0.1

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

	void switchGravity();
	void fall();
	bool getG();

	void changeDirection(glm::vec3 direction);

	void defineLookAt(Shader shaderProgram);
private:
	bool gravity = false;
	glm::vec3 cameraPosition;
	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	float cameraSpeed;
};