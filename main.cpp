#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>

#include <vector>

#include "shader.h"
#include "texture.h"
#include "cube.h"
#include "chunk.h"
#include "camera.h"
#include "world.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 1000

int windowWidth = WINDOW_WIDTH;
int windowHeight = WINDOW_HEIGHT;

Camera camera = Camera();
glm::ivec3 targeted = glm::ivec3(0);

World* w = nullptr;

bool xKeyPressed = false;
bool wireframeMode = false;

bool f12KeyPressed = false;
bool fullscreenMode = false;

bool previousGravity = false;
bool gravity = false;


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width axnd 
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
	camera.changeDirection(direction);
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
		// La touche "X" vient d'�tre enfonc�e
		// Inverser l'�tat du mode wireframe
		wireframeMode = !wireframeMode;
		if (wireframeMode) {
			// Activer le mode wireframe
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else {
			// D�sactiver le mode wireframe
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}
	xKeyPressed = xKeyDown;

	if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
		// V�rifiez l'�tat pr�c�dent de la touche "G"
		if (!previousGravity) {
			// Inversez la valeur du bool�en
			camera.switchGravity();
			gravity = !gravity;
		}
		// Mettez � jour l'�tat pr�c�dent de la touche "G"
		previousGravity = true;
	}
	else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
		// R�initialisez l'�tat pr�c�dent lorsque la touche "G" est rel�ch�e
		previousGravity = false;
	}

	// Camera moves
	// Forward
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.forward();
	// Back
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.back();
	// Left
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.left();
	// Right
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.right();
	// Up
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		camera.up();
	// Down
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		camera.down();
	// Run
	if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
		camera.speedUp();
	else
		camera.resetSpeed();

	// Enable/Disable fullscreen mode
	bool f12KeyDown = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
	if (f12KeyDown && !f12KeyPressed) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
		fullscreenMode = !fullscreenMode;
		if (fullscreenMode && glfwGetWindowMonitor(window) == nullptr) {
			// Activer le mode fullscreen
			glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
		}
		else {
			// D�sactiver le mode fullscreen
			glfwSetWindowMonitor(window, nullptr, 100, 100, 800, 600, GLFW_DONT_CARE);
		}
	}
	f12KeyPressed = f12KeyDown;

	if (w != nullptr && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		w->destroyBlock(targeted);
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

	glEnable(GL_DEPTH_TEST);

	int nrAttributes;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
	std::cout << "Maximum number of vertex attributes supported: " << nrAttributes << std::endl;
	

	Shader shaderProgram("./Shaders/vert.shd", "./Shaders/frag.shd");
	World world = World();
	w = &world;
	//shaderProgram.setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
	//Chunk chunk = Chunk();
	//Cube cube = Cube(7, 15, 7, GLOWSTONE);
	//std::vector<Cube> cubes;
	//cubes.push_back(cube);
	//cubes.push_back(Cube(0, 0, 1, "./Textures/grass.png"));

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);

	double lastTime = glfwGetTime();
	int nbFrames = 0;
	while (!glfwWindowShouldClose(window)) {
		//glClearColor(0.53f, 0.82f, 0.98f, 1.0f);
		glClearColor(0.2f, 0.2f, 0.4f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		camera.fall();

		double currentTime = glfwGetTime();
		nbFrames++;
		if (currentTime - lastTime >= 1.0) { // If last prinf() was more than 1 sec ago
			// printf and reset timer
			std::stringstream ss;
			ss << "POGL" << " [" << nbFrames << " FPS]";

			glfwSetWindowTitle(window, ss.str().c_str());
			nbFrames = 0;
			lastTime += 1.0;
		}

		processInput(window);

		shaderProgram.use();
		glm::vec3 lightPos(30, 100.0f, 30);
		glm::vec3 lightColor(1.0f, 1.0f, 1.0f); // white light
		shaderProgram.setVec3("lightPos", lightPos);
		shaderProgram.setVec3("lightColor", lightColor);
		//shaderProgram.setVec3("lightPosition", cube.getPosition() - glm::vec3(0.5, 0.5, 0.5));
		//shaderProgram.setVec3("lightColor", glm::vec3(1.0, 1.0, 1.0));
		camera.defineLookAt(shaderProgram);
		glm::vec3 cameraPos = camera.getPosition();

		shaderProgram.setVec3("viewPos", cameraPos);
		glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
		shaderProgram.setMat4("projection", projection);

		glm::mat4 model = glm::mat4(1.0f);
		shaderProgram.setMat4("model", model);

		glm::vec2 windowSize = glm::vec2(windowWidth, windowHeight);
		shaderProgram.setVec2("windowSize", windowSize);

		glm::vec3 targetPosition = camera.getTargetPosition();

		int targetBlockX = static_cast<int>(std::round(targetPosition.x));
		int targetBlockY = static_cast<int>(std::round(targetPosition.y));
		int targetBlockZ = static_cast<int>(std::round(targetPosition.z));
		std::cout << "X: " << targetBlockX << " Y: " << targetBlockY << " Z: " << targetBlockZ << std::endl;
		targeted = glm::ivec3(targetBlockX, targetBlockY, targetBlockZ);
		shaderProgram.setVec3("targeted", targeted);

		/*
		for (int i = 0; i < 2; i++) {
			glm::vec3 translation = glm::vec3(cubes[i].getPosition());
			shaderProgram.setVec3("translation", translation);
			cubes.at(i).render();
		}*/
		//cube.render(shaderProgram);
		//chunk.render(shaderProgram);
		//chunk2.render(shaderProgram);
		//chunk3.render(shaderProgram);
		//chunk4.render(shaderProgram);
		world.render(shaderProgram);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	shaderProgram.destroy();

	glfwDestroyWindow(window);
	std::cout << "Window destroyed" << std::endl;
	glfwTerminate();
	std::cout << "GLFW terminated" << std::endl;
	return 0;
}