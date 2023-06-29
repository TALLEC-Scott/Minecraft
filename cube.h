#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "texture.h"

class Cube {
public:
	Cube();
	Cube(int x, int y, int z, Texture texture);
	Cube(int x, int y, int z, const char* texturePath);

	void initialize();
	void bind();
	void draw();
	void render();
	void destroy();

	glm::vec3 getPosition();

	~Cube();
private:
	int x;
	int y;
	int z;
	Texture texture;
	unsigned int VAO, VBO, EBO;
};