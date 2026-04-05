#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>

#include <vector>
#include <iomanip>
#include <fstream>
#include <algorithm>

#include "profiler.h"

#include "shader.h"
#include "texture.h"
#include "cube.h"
#include "chunk.h"
#include "camera.h"
#include "world.h"
#include "ChunkManager.h"
#include "texture_array.h"

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

bool doDaylightCycle = true;
bool previousDaylight = false;


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

void setSkyColor(float angle) {
    glm::vec3 noonColor(0.2f, 0.6f, 1.0f);
    glm::vec3 duskColor(0.15f, 0.15f, 0.25f);

    // Factor in range [0, 1]
    float t = (cos(angle) + 1.0f) * 0.5f;

    // Interpolate between the colors based on the cosine of the angle
    glm::vec3 skyColor = duskColor * (1.0f - t) + noonColor * t;

    glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
}


void processInput(GLFWwindow* window) {
	// Close window
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, true);
	}

	// Enable/Disable wireframe mode
	bool xKeyDown = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;
	if (xKeyDown && !xKeyPressed) {
		wireframeMode = !wireframeMode;
		if (wireframeMode) {
			// Enable wireframe mode
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else {
			// Disable wireframe mode
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}
	xKeyPressed = xKeyDown;

	// Enable/Disable gravity
	if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
		if (!previousGravity) {
			camera.switchGravity();
			gravity = !gravity;
		}
		previousGravity = true;
	}
	else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
		previousGravity = false;
	}

	// Enable/Disable daylight cycle
	if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
		if (!previousDaylight) {
			doDaylightCycle = !doDaylightCycle;
		}
		previousDaylight = true;
	}
	else if (glfwGetKey(window, GLFW_KEY_J) == GLFW_RELEASE) {
		previousDaylight = false;
	}

	/** Camera moves **/
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
	if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS || (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS))
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
			// Enable fullscreen mode
			glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
		}
		else {
			// Disable fullscreen mode
			glfwSetWindowMonitor(window, nullptr, 100, 100, 800, 600, GLFW_DONT_CARE);
		}
	}
	f12KeyPressed = f12KeyDown;

	if (w != nullptr && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		w->destroyBlock(targeted);
	}
}

int main(int argc, char* argv[]) {
	bool benchmarkMode = false;
	bool headlessMode = false;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--benchmark") benchmarkMode = true;
		else if (arg == "--headless") headlessMode = true;
	}
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

	glfwSetInputMode(window, GLFW_CURSOR_NORMAL, GLFW_CURSOR_DISABLED);
	glfwSetCursorPos(window, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);

	glfwSetCursorPosCallback(window, cursorPositionCallback);

	if (benchmarkMode) glfwSwapInterval(0); // Disable vsync only for benchmarking

	// Headless mode: create an FBO so rendering goes to an offscreen buffer
	// instead of through the WSL2/D3D12 presentation pipeline.
	GLuint headlessFBO = 0, headlessColor = 0, headlessDepth = 0;
	if (headlessMode) {
		glfwHideWindow(window);
		glGenFramebuffers(1, &headlessFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, headlessFBO);

		glGenTextures(1, &headlessColor);
		glBindTexture(GL_TEXTURE_2D, headlessColor);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, headlessColor, 0);

		glGenRenderbuffers(1, &headlessDepth);
		glBindRenderbuffer(GL_RENDERBUFFER, headlessDepth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WINDOW_WIDTH, WINDOW_HEIGHT);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, headlessDepth);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "WARNING: Headless FBO incomplete!" << std::endl;
	}

	glEnable(GL_DEPTH_TEST);

	int nrAttributes;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
	std::cout << "Maximum number of vertex attributes supported: " << nrAttributes << std::endl;
	

	TextureArray::initialize();
	{
	Shader shaderProgram("./Shaders/vert.shd", "./Shaders/frag.shd");
	World world = World();
	w = &world;

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);

	// Sun billboard quad
	GLuint sunVAO, sunVBO, sunEBO;
	glGenVertexArrays(1, &sunVAO);
	glGenBuffers(1, &sunVBO);
	glGenBuffers(1, &sunEBO);
	unsigned int sunIdx[] = {0,1,2, 2,3,0};
	glBindVertexArray(sunVAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIdx), sunIdx, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
	glBufferData(GL_ARRAY_BUFFER, 4 * 10 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	constexpr int SUN_STRIDE = 10 * sizeof(float);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(3*sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(5*sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(8*sizeof(float)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(9*sizeof(float)));
	glEnableVertexAttribArray(4);
	glBindVertexArray(0);

	double lastTime = glfwGetTime();
	double lastFrameTime = lastTime;
	int nbFrames = 0;
	int chunksRendered = 0;
    glm::vec3 lightColor(1.0f, 1.0f, 1.0f); // white light



   // ChunkManager chunkManager(1, 16, *w->terrainGenerator); // renderDistance=5, chunkSize=16


    // Benchmark mode: fixed camera, warm-up + measured frames, write results to file
    if (benchmarkMode) {
        constexpr int WARMUP_FRAMES  = 600;
        constexpr int MEASURE_FRAMES = 600;

        // Find label: first arg that isn't a flag
        std::string label = "benchmark";
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a[0] != '-') { label = a; break; }
        }

        Profiler profiler;
        profiler.init();

        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
        glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
        int frame = 0;

        while (!glfwWindowShouldClose(window) && frame < WARMUP_FRAMES + MEASURE_FRAMES) {
            bool measuring = (frame >= WARMUP_FRAMES);
            if (measuring) profiler.beginFrame();

            if (frame < WARMUP_FRAMES) {
                // Phase 1 (warmup): spin 360° in place so surrounding chunks load
                float angle = glm::radians((float)frame / WARMUP_FRAMES * 360.0f);
                camera.changeDirection(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
            } else {
                // Phase 2 (measured): move forward at sprint speed, stay horizontal
                camera.changeDirection(glm::vec3(1.0f, 0.0f, 0.0f));
                camera.speedUp();
                camera.forward();
            }

            setSkyColor(0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            shaderProgram.use();

            glm::vec3 lightPos((CHUNK_SIZE * RENDER_DISTANCE) / 2, 1000.0f, 0.0f);
            shaderProgram.setVec3("lightPos", lightPos);
            shaderProgram.setVec3("lightColor", lightColor);
            shaderProgram.setMat4("projection", projection);
            shaderProgram.setMat4("model", glm::mat4(1.0f));
            camera.defineLookAt(shaderProgram);

            TextureArray::bind();

            if (measuring) profiler.beginUpdate();
            w->update(camera.getPosition());
            if (measuring) profiler.endUpdate();

            glm::mat4 vp = projection * camera.getViewMatrix();

            double t0 = glfwGetTime();
            if (measuring) profiler.beginRender();
            w->render(shaderProgram, vp, camera.getPosition());
            if (measuring) profiler.endRender();

            if (measuring) profiler.beginSwap();
            if (headlessMode)
                glFinish();  // Drain GPU pipeline without WSL2 presentation overhead
            else
                glfwSwapBuffers(window);
            if (measuring) profiler.endSwap();
            double t1 = glfwGetTime();

            if (measuring) {
                profiler.recordLegacyFrameTime((t1 - t0) * 1000.0);
                profiler.endFrame((int)w->chunkManager->chunks.size());
            }

            glfwPollEvents();
            frame++;
        }

        profiler.writeLegacyResults(MEASURE_FRAMES);
        profiler.report(label);
        profiler.destroy();

        std::cout << "\nBenchmark complete. Results written to benchmark_results.txt + profile_results.txt" << std::endl;
        shaderProgram.destroy();
        goto cleanup;
    }

    bool sceneChanged = true; // Initially set to true to render the scene
    while (!glfwWindowShouldClose(window)) {
        float speed = 0.05;
		float timeValue = 0.0f;
		if (doDaylightCycle) {
			timeValue = glfwGetTime() * speed;
		}
        float radius = 1000.0f;

        setSkyColor(timeValue);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		camera.fall();

		double currentTime = glfwGetTime();
		double frameTime = (currentTime - lastFrameTime) * 1000.0; // ms
		lastFrameTime = currentTime;
		nbFrames++;
		if (currentTime - lastTime >= 1.0) {
			int totalChunks = (int)w->chunkManager->chunks.size();
			std::stringstream ss;
			ss << "POGL  |  " << nbFrames << " FPS"
			   << "  |  " << std::fixed << std::setprecision(1) << frameTime << " ms"
			   << "  |  chunks: " << chunksRendered << "/" << totalChunks;
			glfwSetWindowTitle(window, ss.str().c_str());
			nbFrames = 0;
			lastTime += 1.0;
		}

		processInput(window);
		shaderProgram.use();

		glm::vec3 cameraPos = camera.getPosition();

        // Sun orbits east-west (X axis) overhead, centered on camera
        glm::vec3 lightPos(cameraPos.x + cos(timeValue) * radius,
                           sin(timeValue) * radius,
                           cameraPos.z);
        shaderProgram.setVec3("lightPos", lightPos);
		shaderProgram.setVec3("lightColor", lightColor);

		camera.defineLookAt(shaderProgram);

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
		targeted = glm::ivec3(targetBlockX, targetBlockY, targetBlockZ);

        TextureArray::bind();
        w->update(camera.getPosition());
        glm::mat4 viewProjection = projection * camera.getViewMatrix();

        // Render sun billboard (before terrain, no depth write)
        constexpr float SUN_DISTANCE = 800.0f;
        constexpr float SUN_SIZE = 60.0f;
        if (lightPos.y > 0) { // only when sun is above horizon
            glm::vec3 sunDir = glm::normalize(lightPos - cameraPos);
            glm::vec3 sunCenter = cameraPos + sunDir * SUN_DISTANCE;
            // Avoid degenerate cross product when sun is directly overhead
            glm::vec3 upRef = (glm::abs(sunDir.y) > 0.99f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
            glm::vec3 right = glm::normalize(glm::cross(sunDir, upRef)) * SUN_SIZE;
            glm::vec3 up = glm::normalize(glm::cross(right, sunDir)) * SUN_SIZE;

            float sunLayer = (float)TextureArray::SUN_LAYER;
            float sunVerts[40] = {
                // pos(3) + uv(2) + normal(3) + layer(1) + ao(1)
                sunCenter.x-right.x-up.x, sunCenter.y-right.y-up.y, sunCenter.z-right.z-up.z, 0,0, 0,0,1, sunLayer, 1,
                sunCenter.x-right.x+up.x, sunCenter.y-right.y+up.y, sunCenter.z-right.z+up.z, 0,1, 0,0,1, sunLayer, 1,
                sunCenter.x+right.x+up.x, sunCenter.y+right.y+up.y, sunCenter.z+right.z+up.z, 1,1, 0,0,1, sunLayer, 1,
                sunCenter.x+right.x-up.x, sunCenter.y+right.y-up.y, sunCenter.z+right.z-up.z, 1,0, 0,0,1, sunLayer, 1,
            };

            glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sunVerts), sunVerts);

            shaderProgram.setFloat("emissive", 1.0f);
            glDepthMask(GL_FALSE);    // don't write to depth (sun is behind everything)
            glDisable(GL_CULL_FACE);  // billboard visible from both sides
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glBindVertexArray(sunVAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            shaderProgram.setFloat("emissive", 0.0f);
        }

        chunksRendered = w->render(shaderProgram, viewProjection, camera.getPosition());

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	shaderProgram.destroy();
	} // world and shaderProgram destroyed here, while GL context is still valid
	cleanup:
	if (headlessFBO) {
		glDeleteFramebuffers(1, &headlessFBO);
		glDeleteTextures(1, &headlessColor);
		glDeleteRenderbuffers(1, &headlessDepth);
	}
	TextureArray::destroy();

	glfwDestroyWindow(window);
	std::cout << "Window destroyed" << std::endl;
	glfwTerminate();
	std::cout << "GLFW terminated" << std::endl;
	return 0;
}