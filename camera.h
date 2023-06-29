#pragma once

#include <glm/glm.hpp>

#include "shader.h"

#define SPEED 0.1

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

	void changeDirection(glm::vec3 direction);

	void defineLookAt(Shader shaderProgram);
private:
	glm::vec3 cameraPosition;
	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	float cameraSpeed;
};