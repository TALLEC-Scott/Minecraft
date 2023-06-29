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
	};

	GLuint indices[] = {
		// Face avant
		0, 1, 2,
		2, 3, 0,

		// Face arrière
		4, 7, 6,
		6, 5, 4,

		// Face gauche
		0, 4, 5,
		5, 1, 0,

		// Face droite
		3, 2, 6,
		6, 7, 3,

		// Face haut
		1, 5, 6,
		6, 2, 1,

		// Face bas
		7, 4, 3,
		3, 4, 0
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

void Cube::render() {
	initialize();
	glBindVertexArray(this->VAO);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

void Cube::render(Cube* neighbors[6]) {
	initialize();
	glBindVertexArray(this->VAO);

	for (int i = 0; i < 6; i++) {
		if (neighbors[i] == nullptr) {
			// Calcule l'indice de départ pour la face i
			int startIdx = i * 6;

			// Affiche les triangles de la face i en utilisant les indices
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(startIdx * sizeof(GLuint)));
		}
	}

	glBindVertexArray(0);
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

void Cube::setPosition(glm::vec3 position) {
	this->x = position.x;
	this->y = position.y;
	this->z = position.z;
}

Cube::~Cube() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	texture.destroy();
}