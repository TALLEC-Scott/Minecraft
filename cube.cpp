#include "cube.h"

Cube::Cube() : position(0, 0, 0), texture("./Textures/none.png") {
	initialize();
}

Cube::Cube(int x, int y, int z) : position(x, y, z), texture("./Textures/none.png") {
	initialize();
}

Cube::Cube(glm::vec3& position) : position(position), texture("./Textures/none.png") {
	initialize();
}

Cube::Cube(int x, int y, int z, Texture& texture) : position(x, y, z), texture(texture) {
	initialize();
}

Cube::Cube(glm::vec3& position, Texture& texture) : position(position), texture(texture) {
	initialize();
}

Cube::Cube(int x, int y, int z, const char* texturePath) : position(x, y, z), texture(texturePath) {
	initialize();
}

Cube::Cube(glm::vec3& position, const char* texturePath) : position(position), texture(texturePath) {
	initialize();
}

void Cube::initialize() {
	GLfloat vertices[] = {
		// Face avant
		-0.5f, -0.5f, 0.5f,    1.0f, 0.5f, 0.5f,      0.0f, 0.0f,   // Bas gauche : 0
		-0.5f, 0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      0.0f, 1.0f,   // Haut gauche : 1
		0.5f, 0.5f, 0.5f,      1.0f, 0.5f, 0.5f,      1.0f, 1.0f,   // Haut droit : 2
		0.5f, -0.5f, 0.5f,     1.0f, 0.5f, 0.5f,      1.0f, 0.0f,   // Bas droit : 3

		// Face arrière
		// Position				// Color				
		-0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,     1.0f, 0.0f,   // Bas gauche : 4
		-0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,     1.0f, 1.0f,   // Haut gauche : 5
		0.5f, 0.5f, -0.5f,     0.5f, 0.5f, 1.0f,      0.0f, 1.0f,   // Haut droit : 6
		0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,       0.0f, 0.0f,   // Bas droit : 7


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
		-0.5f, 0.5f, -0.5f,    1.0f, 1.0f, 0.5f,      0.0f, 1.0f,   // Haut gauc he : 5
		0.5f, 0.5f, -0.5f,     1.0f, 1.0f, 0.5f,      1.0f, 1.0f,   // Haut droit : 6
		0.5f, 0.5f, 0.5f,      1.0f, 1.0f, 0.5f,      1.0f, 0.0f,   // Bas droit : 2

		// Face bas
		-0.5f, -0.5f, -0.5f,   0.5f, 0.5f, 1.0f,      0.0f, 0.0f,   // Bas gauche : 4
		-0.5f, -0.5f, 0.5f,    0.5f, 0.5f, 1.0f,      0.0f, 1.0f,   // Haut gauche : 0
		0.5f, -0.5f, 0.5f,     0.5f, 0.5f, 1.0f,      1.0f, 1.0f,   // Haut droit : 3
		0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 1.0f,      1.0f, 0.0f    // Bas droit : 7
	};

	GLuint indices[] = {
		// Face avant
		0, 1, 2,
		2, 3, 0,

		// Face arrière
		4, 7, 6,
		6, 5, 4,

		// Face gauche
		8, 9, 10,
		10, 11, 8,

		// Face droite
		12, 15, 14,
		14, 13, 12,

		// Face haut
		16, 17, 18,
		18, 19, 16,

		// Face bas
		23, 20, 22,
		22, 20, 21
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
}

void Cube::render() {
	glBindVertexArray(this->VAO);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

void Cube::render(Cube* neighbors[6]) {
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

void Cube::render(const std::vector<Cube*>& neighbors) {
	texture.bind();
	glBindVertexArray(this->VAO);
	for (int i = 0; i < 6; i++) {
		Cube* block = neighbors[i];
		if (block == nullptr) {
			// Calcule l'indice de départ pour la face i
			int startIdx = i * 6;

			// Affiche les triangles de la face i en utilisant les indices
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)(startIdx * sizeof(GLuint)));
		}
	}
	glBindVertexArray(0);
}

void Cube::translate(GLfloat x, GLfloat y, GLfloat z) {
	this->position.x += x;
	this->position.y += y;
	this->position.z += z;
}

void Cube::destroy() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	texture.destroy();
}

glm::vec3 Cube::getPosition() const {
	return this->position;
}

void Cube::setPosition(glm::vec3& position) {
	this->position = position;
}

void Cube::setPosition(int x, int y, int z) {
	this->position.x = x;
	this->position.y = y;
	this->position.z = z;
}

void Cube::setTexture(const char* texturePath) {
	this->texture = Texture(texturePath);
}

void Cube::setTexture(Texture& texture) {
	this->texture = texture;
}

Cube::~Cube() {
	destroy();
}