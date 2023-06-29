#include "cube.h"

Cube::Cube() {
	this->x = 0;
	this->y = 0;
	this->z = 0;
	this->texture = NULL;
	glGenVertexArrays(1, &(this->VAO));
	glGenBuffers(1, &(this->VBO));
	glGenBuffers(1, &(this->EBO));
}

Cube::Cube(int x, int y, int z, Texture texture) {
	this->x = x;
	this->y = y;
	this->z = z;
	this->texture = texture;
	glGenVertexArrays(1, &(this->VAO));
	glGenBuffers(1, &(this->VBO));
	glGenBuffers(1, &(this->EBO));
}

Cube::Cube(int x, int y, int z, const char* texturePath) {
	this->x = x;
	this->y = y;
	this->z = z;
	this->texture = Texture(texturePath);
	glGenVertexArrays(1, &(this->VAO));
	glGenBuffers(1, &(this->VBO));
	glGenBuffers(1, &(this->EBO));
}

void Cube::initialize() {
	GLfloat vertices[] = {
		// Face avant
		-0.5f, -0.5f, 0.5f,    1.0f, 0.5f, 0.5f,      0.0f, 0.0f,   // Bas gauche : 0
		-0.5f, 0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      0.0f, 1.0f,   // Haut gauche : 1
		0.5f, 0.5f, 0.5f,      1.0f, 0.5f, 0.5f,      1.0f, 1.0f,   // Haut droit : 2
		0.5f, -0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      1.0f, 0.0f,   // Bas droit : 3

		// Face arrière
		-0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 0.0f,   // Bas gauche : 4
		-0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      0.0f, 1.0f,   // Haut gauche : 5
		0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 1.0f,   // Haut droit : 6
		0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,      1.0f, 0.0f,   // Bas droit : 7

		// Face gauche
		-0.5f, -0.5f, -0.5f,   0.5f, 1.0f, 0.5f,      0.0f, 0.0f,   // Bas gauche : 0
		-0.5f, 0.5f, -0.5f,    0.5f, 1.0f, 0.5f,      0.0f, 1.0f,   // Haut gauche : 4
		-0.5f, 0.5f, 0.5f,     0.5f, 1.0f, 0.5f,      1.0f, 1.0f,   // Haut droit : 5
		-0.5f, -0.5f, 0.5f,    0.5f, 1.0f, 0.5f,      1.0f, 0.0f,   // Bas droit : 1

		// Face droite
		0.5f, -0.5f, 0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 0.0f,   // Bas gauche : 3
		0.5f, -0.5f, -0.5f,      0.5f, 0.5f, 1.0f,      0.0f, 0.0f,   // Haut gauche : 7
		0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      0.0f, 1.0f,   // Haut droit : 6
		0.5f, 0.5f, 0.5f,    0.5f, 0.5f, 1.0f,      1.0f, 1.0f,   // Bas droit : 2

		// Face haut
		-0.5f, 0.5f, 0.5f,     1.0f, 1.0f, 0.5f,      0.0f, 0.0f,   // Bas gauche : 1
		-0.5f, 0.5f, -0.5f,    1.0f, 1.0f, 0.5f,      0.0f, 1.0f,   // Haut gauche : 5
		0.5f, 0.5f, -0.5f,     1.0f, 1.0f, 0.5f,      1.0f, 1.0f,   // Haut droit : 6
		0.5f, 0.5f, 0.5f,      1.0f, 1.0f, 0.5f,      1.0f, 0.0f,   // Bas droit : 2

		// Face bas
		-0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,      0.0f, 0.0f,   // Bas gauche : 4
		-0.5f, -0.5f, 0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 1.0f,   // Haut gauche : 0
		0.5f, -0.5f, 0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 1.0f,   // Haut droit : 3
		0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      1.0f, 0.0f    // Bas droit : 7
	};

	GLuint indices[] = {
		20, 23, 22, 22, 21, 20,
		16, 19, 18, 18, 17, 16,
		12, 15, 14, 14, 13, 12,
		8, 11, 10, 10, 9, 8,
		4, 7, 6, 6, 5, 4,
		0, 3, 2, 2, 1, 0
	};

	glGenVertexArrays(1, &this->VAO);
	glGenBuffers(1, &(this->VBO));
	glGenBuffers(1, &(this->EBO));

	glBindVertexArray(this->VAO);

	glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)0);
	glEnableVertexAttribArray(0);;

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	texture.bind();
}

void Cube::bind() {
	glBindVertexArray(this->VAO);
}

void Cube::draw() {
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

void Cube::render() {
	initialize();
	bind();
	draw();
}

void Cube::destroy() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	texture.destroy();
}

glm::vec3 Cube::getPosition() {
	return glm::vec3(x, y, z);
}

Cube::~Cube() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	texture.destroy();
}