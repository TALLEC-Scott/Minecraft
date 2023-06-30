#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "texture.h"

class Cube {
public:
	Cube();
	Cube(int x, int y, int z);
	Cube(glm::vec3 position);
	Cube(int x, int y, int z, Texture texture);
	Cube(glm::vec3 position, Texture texture);
	Cube(int x, int y, int z, const char* texturePath);
	Cube(glm::vec3 position, const char* texturePath);

	void initialize();
	void render();
	void render(Cube* neighbors[6]);
	void render(const std::vector<Cube*>& neighbors);
	void destroy();

	glm::vec3 getPosition();

	void setPosition(glm::vec3 position);
	void setPosition(int x, int y, int z);

	~Cube();
private:
	glm::vec3 position;
	Texture texture;
	unsigned int VAO, VBO, EBO;
};