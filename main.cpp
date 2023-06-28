#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>

#include "shader.h"
#include "texture.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 1000

#define SPEED 0.001

int windowWidth = WINDOW_WIDTH;
int windowHeight = WINDOW_HEIGHT;

glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float cameraSpeed = SPEED;

bool xKeyPressed = false;
bool wireframeMode = false;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and 
	// height will be significantly larger than specified on retina displays.
	glViewport(0, 0, width, height);
	windowWidth = width;
	windowHeight = height;
}

void cursorPositionCallback(GLFWwindow* window, double xPos, double yPos) {
	static double lastX = WINDOW_WIDTH / 2;
	static double lastY = WINDOW_HEIGHT / 2;
	static bool firstMouse = true;
	static float yaw = -90.0f;
	static float pitch = 0.0f;

	if (firstMouse) {
		lastX = xPos;
		lastY = yPos;
		firstMouse = false;
	}

	float sensitivity = 0.1f;
	float xOffset = xPos - lastX;
	float yOffset = lastY - yPos;
	lastX = xPos;
	lastY = yPos;

	xOffset *= sensitivity;
	yOffset *= sensitivity;

	yaw += xOffset;
	pitch += yOffset;

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	glm::vec3 direction;
	direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	direction.y = sin(glm::radians(pitch));
	direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	cameraFront = glm::normalize(direction);
}

void processInput(GLFWwindow* window)
{
	// Close window
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, true);
	}

	// Enable/Disable wireframe mode
	bool xKeyDown = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;
	if (xKeyDown && !xKeyPressed) {
		// La touche "X" vient d'être enfoncée
		// Inverser l'état du mode wireframe
		wireframeMode = !wireframeMode;
		if (wireframeMode) {
			// Activer le mode wireframe
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else {
			// Désactiver le mode wireframe
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}
	xKeyPressed = xKeyDown;

	// Camera moves
	// Forward
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		cameraPosition += cameraSpeed * cameraFront;
	// Back
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		cameraPosition -= cameraSpeed * cameraFront;
	// Left
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		cameraPosition -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	// Right
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		cameraPosition += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
	// Up
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		cameraPosition += cameraSpeed * cameraUp;
	// Down
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		cameraPosition -= cameraSpeed * cameraUp;
	// Run
	if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
		cameraSpeed = 4 * SPEED;
	else
		cameraSpeed = SPEED;

	// Enable/Disable fullscreen
	if (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);

		if (glfwGetWindowMonitor(window) == nullptr) {
			glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
		}
		else {
			glfwSetWindowMonitor(window, nullptr, 100, 100, 800, 600, GLFW_DONT_CARE);
		}
	}
}

int main(void) {
	if (!glfwInit()) {
		std::cout << "Failed to initialize GLFW" << std::endl;
		return -1;
	}
	std::cout << "GLFW successfully initialized" << std::endl;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	std::cout << "Using OpenGL 3.3 Core Profile" << std::endl;

	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "POGL", NULL, NULL);
	if (window == NULL) {
		std::cout << "Failed to create window" << std::endl;
		glfwTerminate();
		return -1;
	}
	std::cout << "Window successfully created" << std::endl;
	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		glfwTerminate();
		return -1;
	}
	std::cout << "Glad successfully loaded" << std::endl;

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPos(window, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);

	glfwSetCursorPosCallback(window, cursorPositionCallback);

	int nrAttributes;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
	std::cout << "Maximum number of vertex attributes supported: " << nrAttributes << std::endl;

	GLfloat vertices[] = {
		// Position								// Color						// Texture
		-0.5, -0.5, 0,	/*BOTTOM  LEFT*/		1, 0.5, 0.5,  /*RED   */		0, 0,  /*BOTTOM  LEFT*/
		-0.5,  0.5, 0,	/*TOP	  LEFT*/		1, 0.5, 0.5,  /*GREEN */		0, 1,  /*TOP     LEFT*/
		 0.5,  0.5, 0,	/*TOP    RIGHT*/		1, 0.5, 0.5,  /*BLUE  */		1, 1,  /*TOP    RIGHT*/
		 0.5, -0.5, 0,	/*BOTTOM RIGHT*/		1, 0.5, 0.5,   /*YELLOW*/		1, 0   /*BOTTOM RIGHT*/
	};

	GLuint indices[] = {
		0, 1, 2,
		2, 0, 3,
	};

	Shader shaderProgram("./Shaders/vert.shd", "./Shaders/frag.shd");

	unsigned int VAO, VBO, EBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)0);
	glEnableVertexAttribArray(0);;

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	Texture texture("./Textures/grass.png");
	texture.bind();

	double lastTime = glfwGetTime();
	int nbFrames = 0;

	while (!glfwWindowShouldClose(window)) {
		double currentTime = glfwGetTime();
		nbFrames++;
		if (currentTime - lastTime >= 1.0) { // If last prinf() was more than 1 sec ago
			// printf and reset timer
			std::cout << double(nbFrames) << " fps" << std::endl;
			nbFrames = 0;
			lastTime += 1.0;
		}

		processInput(window);

		glClearColor(0.53f, 0.82f, 0.98f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		shaderProgram.use();
		glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);
		shaderProgram.setMat4("view", view);

		glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);
		shaderProgram.setMat4("projection", projection);

		glm::mat4 model = glm::mat4(1.0f);
		shaderProgram.setMat4("model", model);

		glm::vec2 windowSize = glm::vec2(windowWidth, windowHeight);
		shaderProgram.setVec2("windowSize", windowSize);

		glBindVertexArray(VAO);

		// Ajout de l'appel pour activer le shader
		shaderProgram.use();


		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 12, GL_UNSIGNED_INT, 0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);

	shaderProgram.destroy();
	texture.destroy();

	glfwDestroyWindow(window);
	std::cout << "Window destroyed" << std::endl;
	glfwTerminate();
	std::cout << "GLFW terminated" << std::endl;
	return 0;
}