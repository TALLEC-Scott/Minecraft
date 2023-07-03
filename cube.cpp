/**
 * @file cube.cpp
 * @brief Implements the Cube class, representing a cube object in a 3D world.
 */

#include "cube.h"

/**
 * @brief Constructs a cube with the default position (0, 0, 0) and type AIR.
 *
 * This constructor creates a cube with the default position at the origin (0, 0, 0)
 * and the type set to AIR.
 */
Cube::Cube() : position(0, 0, 0), type(AIR) {
	initialize();

}

/**
 * @brief Constructs a cube with the specified position coordinates and type AIR.
 *
 * This constructor creates a cube with the specified position coordinates (x, y, z)
 * and the type set to AIR.
 *
 * @param x The x-coordinate of the cube's position.
 * @param y The y-coordinate of the cube's position.
 * @param z The z-coordinate of the cube's position.
 */
Cube::Cube(int x, int y, int z) : position(x, y, z), type(AIR) {
	initialize();
}


/**
 * @brief Constructs a cube with the specified position and type AIR.
 *
 * This constructor creates a cube with the specified position given as a `glm::vec3`
 * object and the type set to AIR.
 *
 * @param position The position of the cube as a `glm::vec3`.
 */
Cube::Cube(glm::vec3& position) : position(position), type(AIR) {
	initialize();
}

/**
 * @brief Constructs a cube with the specified position coordinates and type.
 *
 * This constructor creates a cube with the specified position coordinates (x, y, z)
 * and the specified type.
 *
 * @param x The x-coordinate of the cube's position.
 * @param y The y-coordinate of the cube's position.
 * @param z The z-coordinate of the cube's position.
 * @param type The type of the cube.
 */
Cube::Cube(int x, int y, int z, block_type type) : position(x, y, z), type(type) {
	initialize();
}

/**
 * @brief Constructs a cube with the specified position and type.
 *
 * This constructor creates a cube with the specified position given as a `glm::vec3`
 * object and the specified type.
 *
 * @param position The position of the cube as a `glm::vec3`.
 * @param type The type of the cube.
 */
Cube::Cube(glm::vec3& position, block_type type) : position(position), type(type) {
	initialize();
}

/**
 * @brief Renders the cube using OpenGL.
 *
 * This function renders the cube using OpenGL. If the cube's type is AIR,
 * the cube will not be rendered. Otherwise, the cube will be rendered using
 * the associated VAO (Vertex Array Object) and the glDrawElements function.
 */
void Cube::render() {
	if (type != AIR) {
		texture.bind();
		glBindVertexArray(this->VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
	}
}

void Cube::render(Shader shaderProgram) {
	if (type != AIR) {
		texture.bind();
		shaderProgram.setVec3("translation", getPosition());
		shaderProgram.use();
		glBindVertexArray(this->VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
	}
}

/**
 * @brief Renders the cube using OpenGL, considering neighboring cubes.
 *
 * This function renders the cube using OpenGL. If the cube's type is AIR, the cube
 * will not be rendered. Otherwise, the cube will be rendered using the associated
 * VAO (Vertex Array Object) and the glDrawElements function.
 *
 * The rendering of individual faces of the cube is conditional on the presence of
 * neighboring cubes. If a neighboring cube is either nullptr or of type AIR, the
 * corresponding face will be rendered. This behavior allows for the rendering of
 * only the exposed faces of the cube, creating a seamless appearance when adjacent
 * cubes are present.
 *
 * @param neighbors A vector containing pointers to neighboring cubes.
 *                  The order of neighbors should correspond to the following directions:
 *                  [Front, Back, Left, Right, Top, Bottom]
 */
void Cube::render(const std::vector<Cube*>& neighbors) {
	if (type == AIR) {
		return;
	}
	texture.bind();
	glBindVertexArray(this->VAO);
	for (int i = 0; i < 6; i++) {
		Cube* block = neighbors[i];
		if (block == nullptr && i == 4) {
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(4 * 6 * sizeof(GLuint)));
		}
		else if (block != NULL && block->getType() == AIR) {
			// Calculate the starting index for face i
			int startIdx = i * 6;

			// Render the triangles of face i using the indices
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(startIdx * sizeof(GLuint)));
		}
	}
	glBindVertexArray(0);
}

/**
 * @brief Translates the cube by the specified amount in the x, y, and z directions.
 *
 * This function translates the cube by the specified amounts in the x, y, and z directions.
 * The translation is applied to the cube's current position, updating its coordinates accordingly.
 *
 * @param x The amount to translate the cube in the x direction.
 * @param y The amount to translate the cube in the y direction.
 * @param z The amount to translate the cube in the z direction.
 */
void Cube::translate(GLfloat x, GLfloat y, GLfloat z) {
	this->position.x += x;
	this->position.y += y;
	this->position.z += z;
}

/**
 * @brief Destroys the Cube object and releases any allocated resources.
 *
 * This function deletes the vertex array object (VAO), vertex buffer object (VBO),
 * and element buffer object (EBO) associated with the Cube. It also calls the
 * `destroy()` function of the texture object to release any resources used by it.
 */
void Cube::destroy() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	texture.destroy();
}

/**
 * @brief Gets the position of the cube.
 *
 * @return The position of the cube as a glm::vec3.
 */
glm::vec3 Cube::getPosition() const {
	return this->position;
}

/**
 * @brief Gets the type of the cube.
 *
 * @return The type of the cube as a block_type enum.
 */
block_type Cube::getType() const {
	return this->type;
}

/**
 * @brief Sets the position of the cube.
 *
 * @param x The new x-coordinate of the cube.
 * @param y The new y-coordinate of the cube.
 * @param z The new z-coordinate of the cube.
 */
void Cube::setPosition(int x, int y, int z) {
	this->position.x = x;
	this->position.y = y;
	this->position.z = z;
}

/**
 * @brief Sets the position of the cube.
 *
 * @param position The new position of the cube as a glm::vec3.
 */
void Cube::setPosition(glm::vec3& position) {
	this->position = position;
}

/**
 * @brief Sets the type of the cube.
 *
 * @param type The new type of the cube as a block_type enum.
 */
void Cube::setType(block_type type) {
	this->type = type;
	defineTexture();
}

/**
 * @brief Initializes the cube by defining its texture and setting up the vertex and index buffers.
 *
 * This function is called during the cube's construction to set up its texture and
 * initialize the vertex and index buffers required for rendering.
 * It defines the cube's vertices, colors, texture coordinates, and indices for each face.
 * The vertex array object (VAO), vertex buffer object (VBO), and element buffer object (EBO) are generated,
 * and the data is bound and stored in the respective buffers.
 */
void Cube::initialize() {
	defineTexture();

    GLfloat vertices[] = {
            /*      Position               Color                  Texture          Normal         */
            // Front face
            -0.5f, -0.5f, 0.5f,    1.0f, 0.5f, 0.5f,      0.0f, 0.0f,      0.0f, 0.0f, 1.0f,   // Bottom left
            -0.5f, 0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      0.0f, 1.0f,      0.0f, 0.0f, 1.0f,   // Top left
            0.5f, 0.5f, 0.5f,      1.0f, 0.5f, 0.5f,      1.0f, 1.0f,      0.0f, 0.0f, 1.0f,   // Top right
            0.5f, -0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      1.0f, 0.0f,      0.0f, 0.0f, 1.0f,   // Bottom right

            // Back face
            -0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,      1.0f, 0.0f,      0.0f, 0.0f, -1.0f,   // Bottom left
            -0.5f, 0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      1.0f, 1.0f,      0.0f, 0.0f, -1.0f,   // Top left
            0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      0.0f, 1.0f,      0.0f, 0.0f, -1.0f,   // Top right
            0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 0.0f,      0.0f, 0.0f, -1.0f,   // Bottom right

            // Left face
            -0.5f, -0.5f, -0.5f,   0.5f, 1.0f, 0.5f,      0.0f, 0.0f,      -1.0f, 0.0f, 0.0f,   // Bottom left
            -0.5f, 0.5f, -0.5f,    0.5f, 1.0f, 0.5f,      0.0f, 1.0f,      -1.0f, 0.0f, 0.0f,   // Top left
            -0.5f, 0.5f, 0.5f,     0.5f, 1.0f, 0.5f,      1.0f, 1.0f,      -1.0f, 0.0f, 0.0f,   // Top right
            -0.5f, -0.5f, 0.5f,    0.5f, 1.0f, 0.5f,      1.0f, 0.0f,      -1.0f, 0.0f, 0.0f,   // Bottom right

            // Right face
            0.5f, -0.5f, 0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 0.0f,      1.0f, 0.0f, 0.0f,   // Bottom left
            0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 0.0f,      1.0f, 0.0f, 0.0f,   // Top left
            0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      0.0f, 1.0f,      1.0f, 0.0f, 0.0f,   // Top right
            0.5f, 0.5f, 0.5f,      0.5f, 0.5f, 1.0f,      1.0f, 1.0f,      1.0f, 0.0f, 0.0f,   // Bottom right

            // Top face
            -0.5f, 0.5f, 0.5f,     1.0f, 1.0f, 0.5f,      0.0f, 0.0f,      0.0f, 1.0f, 0.0f,   // Bottom left
            -0.5f, 0.5f, -0.5f,    1.0f, 1.0f, 0.5f,      0.0f, 1.0f,      0.0f, 1.0f, 0.0f,   // Top left
            0.5f, 0.5f, -0.5f,     1.0f, 1.0f, 0.5f,      1.0f, 1.0f,      0.0f, 1.0f, 0.0f,   // Top right
            0.5f, 0.5f, 0.5f,      1.0f, 1.0f, 0.5f,      1.0f, 0.0f,      0.0f, 1.0f, 0.0f,   // Bottom right

            // Bottom face
            -0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,      0.0f, 0.0f,      0.0f, -1.0f, 0.0f,   // Bottom left
            -0.5f, -0.5f, 0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 1.0f,      0.0f, -1.0f, 0.0f,   // Top left
            0.5f, -0.5f, 0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 1.0f,      0.0f, -1.0f, 0.0f,   // Top right
            0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      1.0f, 0.0f,      0.0f, -1.0f, 0.0f    // Bottom right
    };

	GLuint indices[] = {
		// Front face
		0, 1, 2,
		2, 3, 0,


		// Back face

		4, 7, 6,
		6, 5, 4,

		// Left face
		8, 9, 10,
		10, 11, 8,

		// Right face
		12, 15, 14,
		14, 13, 12,

		// Top face
		16, 17, 18,
		18, 19, 16,

		// Bottom face
		23, 20, 22,
		22, 20, 21
	};

	glGenVertexArrays(1, &this->VAO);
	glGenBuffers(1, &(this->VBO));
	glGenBuffers(1, &(this->EBO));

	glBindVertexArray(this->VAO);

	glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// Layout 0 : Position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(GLfloat), (void*)0);
	glEnableVertexAttribArray(0);;

	// Layout 1 : Color
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	// Layout 2 : Texture
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(GLfloat), (void*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

    // Layout 3 : Normal
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(GLfloat), (void*)(11 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
}


/**
 * @brief Defines the texture for the cube based on its type.
 *
 * This function sets the texture for the cube based on its type.
 * It uses a switch statement to determine the appropriate texture file path
 * based on the cube's type and assigns the corresponding texture to the cube.
 * If the cube's type is not recognized, it assigns a default texture.
 */
void Cube::defineTexture() {
	switch (type)
	{
	case AIR:
		this->texture = Texture("./Textures/none.png");
		break;
	case GRASS:
		this->texture = Texture("./Textures/grass.png");
		break;
	case DIRT:
		this->texture = Texture("./Textures/dirt.png");
		break;
	case STONE:
		this->texture = Texture("./Textures/stone.png");
		break;
	case COAL_ORE:
		this->texture = Texture("./Textures/coal_ore.png");
		break;
	case GLOWSTONE:
		this->texture = Texture("./Textures/glowstone.png");
		break;
	case BEDROCK:
		this->texture = Texture("./Textures/bedrock.png");
		break;
	default:
		this->texture = Texture("./Textures/none.png");
		break;
	}
}

/**
 * @brief Destructor for the Cube class.
 *
 * Cleans up any resources allocated by the cube.
 */
Cube::~Cube() {
	destroy();
}