/**
 * @file cube.h
 * @brief Defines the Cube class, representing a cube object in a 3D world.
 */

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "texture.h"
#include "shader.h"

/**
 * @enum block_type
 * @brief Enumerates the types of blocks that a Cube object can represent.
 */
enum block_type {
	AIR,
	GRASS,
	DIRT,
	STONE,
	COAL_ORE,
	BEDROCK,
	WATER,
	SAND,
	GLOWSTONE
};

/**
 * @class Cube
 * @brief Represents a cube object in a 3D world.
 *
 * The Cube class provides functionality for rendering and manipulating a cube object.
 */
class Cube {
public:
	Cube();
	Cube(int x, int y, int z);
	Cube(int x, int y, int z, block_type type);
	Cube(glm::vec3& position);
	Cube(glm::vec3& position, block_type type);

	void render();
	void render(Shader shaderProgram);
	void render(const std::vector<Cube*>& neighbors);
	void destroy();
	void translate(GLfloat x, GLfloat y, GLfloat z);

	glm::vec3 getPosition() const;
	block_type getType() const;

	void setPosition(int x, int y, int z);
	void setPosition(glm::vec3& position);
	void setType(block_type type);

	~Cube();

protected:
	block_type type;            /**< The type of the cube. */
	glm::vec3 position;         /**< The position of the cube. */
	Texture texture;            /**< The texture associated with the cube. */
	unsigned int VAO, VBO, EBO; /**< OpenGL buffer objects for rendering the cube. */

	void initialize();
	void defineTexture();
};