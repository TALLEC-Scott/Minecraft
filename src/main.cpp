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
#include <cmath>

#include "profiler.h"

#include "shader.h"
#include "texture.h"
#include "cube.h"
#include "chunk.h"
#include "camera.h"
#include "world.h"
#include "ChunkManager.h"
#include "texture_array.h"
#include "player.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 1000

int windowWidth = WINDOW_WIDTH;
int windowHeight = WINDOW_HEIGHT;

Player player;
World* w = nullptr;

bool xKeyPressed = false;
bool wireframeMode = false;

bool f12KeyPressed = false;
bool fullscreenMode = false;

bool doDaylightCycle = true;
bool previousDaylight = false;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // make sure the viewport matches the new window dimensions; note that width axnd
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
    windowWidth = width;
    windowHeight = height;
}

void cursorPositionCallback(GLFWwindow* window, double xPos, double yPos) {
    player.updateMouseLook(xPos, yPos, windowWidth, windowHeight);
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
        } else {
            // Disable wireframe mode
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }
    xKeyPressed = xKeyDown;

    // Enable/Disable daylight cycle
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
        if (!previousDaylight) {
            doDaylightCycle = !doDaylightCycle;
        }
        previousDaylight = true;
    } else if (glfwGetKey(window, GLFW_KEY_J) == GLFW_RELEASE) {
        previousDaylight = false;
    }

    // Player input
    player.handleInput(window, w);

    // Enable/Disable fullscreen mode
    bool f12KeyDown = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
    if (f12KeyDown && !f12KeyPressed) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        fullscreenMode = !fullscreenMode;
        if (fullscreenMode && glfwGetWindowMonitor(window) == nullptr) {
            // Enable fullscreen mode
            glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
        } else {
            // Disable fullscreen mode
            glfwSetWindowMonitor(window, nullptr, 100, 100, 800, 600, GLFW_DONT_CARE);
        }
    }
    f12KeyPressed = f12KeyDown;
}

int main(int argc, char* argv[]) {
    bool benchmarkMode = false;
    bool headlessMode = false;
    unsigned int worldSeed = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--benchmark")
            benchmarkMode = true;
        else if (arg == "--headless")
            headlessMode = true;
        else if (arg == "--seed" && i + 1 < argc)
            worldSeed = std::stoul(argv[++i]);
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

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Glad successfully loaded" << std::endl;

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glfwSetInputMode(window, GLFW_CURSOR_NORMAL, GLFW_CURSOR_DISABLED);
    glfwSetCursorPos(window, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);

    glfwSetCursorPosCallback(window, cursorPositionCallback);

    glfwSwapInterval(0); // Disable vsync

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
        Shader shaderProgram("assets/Shaders/vert.shd", "assets/Shaders/frag.shd");
        World world(worldSeed);
        std::cout << "World seed: " << worldSeed << std::endl;
        w = &world;

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);

        // Sun billboard quad
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO);
        glGenBuffers(1, &sunVBO);
        glGenBuffers(1, &sunEBO);
        unsigned int sunIdx[] = {0, 1, 2, 2, 3, 0};
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIdx), sunIdx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
        glBufferData(GL_ARRAY_BUFFER, 4 * 10 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        constexpr int SUN_STRIDE = 10 * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, SUN_STRIDE, (void*)(9 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glBindVertexArray(0);

        // Cloud mesh: precomputed large tiling grid, shifted by model matrix each frame
        GLuint cloudVAO, cloudVBO, cloudEBO;
        int cloudIndexCount = 0;
        {
            constexpr int CLOUD_GRID = 128; // tiling pattern (wraps infinitely)
            constexpr int CLOUD_BLOCK = 12; // larger cloud pieces
            float cl = (float)TextureArray::CLOUD_LAYER;

            // Precompute cloud pattern: low-frequency noise for large connected blobs
            std::vector<float> cverts;
            std::vector<unsigned int> cidx;
            unsigned int cbase = 0;

            for (int gx = 0; gx < CLOUD_GRID; gx++) {
                for (int gz = 0; gz < CLOUD_GRID; gz++) {
                    // Very low frequency → large continuous cloud masses
                    double cn = world.terrainGenerator->getNoise(gx, gz);
                    if (cn < 0.65) continue;

                    float x0 = (gx - CLOUD_GRID / 2) * CLOUD_BLOCK;
                    float x1 = x0 + CLOUD_BLOCK;
                    float z0 = (gz - CLOUD_GRID / 2) * CLOUD_BLOCK;
                    float z1 = z0 + CLOUD_BLOCK;
                    float v[] = {
                        x0, 0, z0, 0, 0, 0, 1, 0, cl, 1, x0, 0, z1, 0, 1, 0, 1, 0, cl, 1,
                        x1, 0, z1, 1, 1, 0, 1, 0, cl, 1, x1, 0, z0, 1, 0, 0, 1, 0, cl, 1,
                    };
                    for (float f : v) cverts.push_back(f);
                    cidx.push_back(cbase);
                    cidx.push_back(cbase + 1);
                    cidx.push_back(cbase + 2);
                    cidx.push_back(cbase + 2);
                    cidx.push_back(cbase + 3);
                    cidx.push_back(cbase);
                    cbase += 4;
                }
            }
            cloudIndexCount = (int)cidx.size();

            glGenVertexArrays(1, &cloudVAO);
            glGenBuffers(1, &cloudVBO);
            glGenBuffers(1, &cloudEBO);
            glBindVertexArray(cloudVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
            glBufferData(GL_ARRAY_BUFFER, cverts.size() * sizeof(float), cverts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cloudEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, cidx.size() * sizeof(unsigned int), cidx.data(), GL_STATIC_DRAW);
            constexpr int CL_STRIDE = 10 * sizeof(float);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, CL_STRIDE, (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, CL_STRIDE, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, CL_STRIDE, (void*)(5 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, CL_STRIDE, (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, CL_STRIDE, (void*)(9 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glBindVertexArray(0);
        }

        // Wireframe highlight cube (12 edges = 24 line vertices)
        GLuint highlightVAO, highlightVBO;
        glGenVertexArrays(1, &highlightVAO);
        glGenBuffers(1, &highlightVBO);
        glBindVertexArray(highlightVAO);
        glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
        glBufferData(GL_ARRAY_BUFFER, 24 * 10 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        constexpr int HL_STRIDE = 10 * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, HL_STRIDE, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, HL_STRIDE, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, HL_STRIDE, (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, HL_STRIDE, (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, HL_STRIDE, (void*)(9 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glBindVertexArray(0);

        // First-person arm mesh (a simple box)
        GLuint armVAO, armVBO, armEBO;
        {
            glGenVertexArrays(1, &armVAO);
            glGenBuffers(1, &armVBO);
            glGenBuffers(1, &armEBO);

            float sl = (float)TextureArray::SKIN_LAYER;
            // Box: 0.3 wide, 0.8 tall, 0.3 deep — positioned in view space
            float w = 0.15f, h = 0.4f, d = 0.15f;
            // 8 corners, 6 faces × 4 verts = 24 verts
            // Each vert: pos(3) + uv(2) + normal(3) + layer(1) + ao(1) = 10
            float verts[] = {
                // Front face (+Z)
                -w,
                -h,
                d,
                0,
                0,
                0,
                0,
                1,
                sl,
                1,
                -w,
                h,
                d,
                0,
                1,
                0,
                0,
                1,
                sl,
                1,
                w,
                h,
                d,
                1,
                1,
                0,
                0,
                1,
                sl,
                1,
                w,
                -h,
                d,
                1,
                0,
                0,
                0,
                1,
                sl,
                1,
                // Back face (-Z)
                w,
                -h,
                -d,
                0,
                0,
                0,
                0,
                -1,
                sl,
                1,
                w,
                h,
                -d,
                0,
                1,
                0,
                0,
                -1,
                sl,
                1,
                -w,
                h,
                -d,
                1,
                1,
                0,
                0,
                -1,
                sl,
                1,
                -w,
                -h,
                -d,
                1,
                0,
                0,
                0,
                -1,
                sl,
                1,
                // Left (-X)
                -w,
                -h,
                -d,
                0,
                0,
                -1,
                0,
                0,
                sl,
                1,
                -w,
                h,
                -d,
                0,
                1,
                -1,
                0,
                0,
                sl,
                1,
                -w,
                h,
                d,
                1,
                1,
                -1,
                0,
                0,
                sl,
                1,
                -w,
                -h,
                d,
                1,
                0,
                -1,
                0,
                0,
                sl,
                1,
                // Right (+X)
                w,
                -h,
                d,
                0,
                0,
                1,
                0,
                0,
                sl,
                1,
                w,
                h,
                d,
                0,
                1,
                1,
                0,
                0,
                sl,
                1,
                w,
                h,
                -d,
                1,
                1,
                1,
                0,
                0,
                sl,
                1,
                w,
                -h,
                -d,
                1,
                0,
                1,
                0,
                0,
                sl,
                1,
                // Top (+Y)
                -w,
                h,
                d,
                0,
                0,
                0,
                1,
                0,
                sl,
                1,
                -w,
                h,
                -d,
                0,
                1,
                0,
                1,
                0,
                sl,
                1,
                w,
                h,
                -d,
                1,
                1,
                0,
                1,
                0,
                sl,
                1,
                w,
                h,
                d,
                1,
                0,
                0,
                1,
                0,
                sl,
                1,
                // Bottom (-Y)
                -w,
                -h,
                -d,
                0,
                0,
                0,
                -1,
                0,
                sl,
                1,
                -w,
                -h,
                d,
                0,
                1,
                0,
                -1,
                0,
                sl,
                1,
                w,
                -h,
                d,
                1,
                1,
                0,
                -1,
                0,
                sl,
                1,
                w,
                -h,
                -d,
                1,
                0,
                0,
                -1,
                0,
                sl,
                1,
            };
            unsigned int indices[36];
            for (int i = 0; i < 6; i++) {
                int b = i * 4;
                indices[i * 6 + 0] = b;
                indices[i * 6 + 1] = b + 1;
                indices[i * 6 + 2] = b + 2;
                indices[i * 6 + 3] = b + 2;
                indices[i * 6 + 4] = b + 3;
                indices[i * 6 + 5] = b;
            }

            glBindVertexArray(armVAO);
            glBindBuffer(GL_ARRAY_BUFFER, armVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, armEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
            constexpr int ARM_STRIDE = 10 * sizeof(float);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(5 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(9 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glBindVertexArray(0);
        }

        double lastTime = glfwGetTime();
        double lastFrameTime = lastTime;
        int nbFrames = 0;
        int chunksRendered = 0;
        glm::vec3 lightColor(1.0f, 1.0f, 1.0f); // white light

        // ChunkManager chunkManager(1, 16, *w->terrainGenerator); // renderDistance=5, chunkSize=16

        // Benchmark mode: fixed camera, warm-up + measured frames, write results to file
        if (benchmarkMode) {
            constexpr int WARMUP_FRAMES = 600;
            constexpr int MEASURE_FRAMES = 600;

            // Find label: first arg that isn't a flag
            std::string label = "benchmark";
            for (int i = 1; i < argc; i++) {
                std::string a = argv[i];
                if (a[0] != '-') {
                    label = a;
                    break;
                }
            }

            Profiler profiler;
            profiler.init();

            glm::mat4 projection =
                glm::perspective(glm::radians(45.0f), (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
            glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
            int frame = 0;

            while (!glfwWindowShouldClose(window) && frame < WARMUP_FRAMES + MEASURE_FRAMES) {
                bool measuring = (frame >= WARMUP_FRAMES);
                if (measuring) profiler.beginFrame();

                if (frame < WARMUP_FRAMES) {
                    // Phase 1 (warmup): spin 360° in place so surrounding chunks load
                    float angle = glm::radians((float)frame / WARMUP_FRAMES * 360.0f);
                    player.getCamera().changeDirection(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
                } else {
                    // Phase 2 (measured): move forward at sprint speed, stay horizontal
                    player.getCamera().changeDirection(glm::vec3(1.0f, 0.0f, 0.0f));
                    player.getCamera().speedUp();
                    player.getCamera().forward();
                }

                setSkyColor(0.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                shaderProgram.use();

                glm::vec3 lightPos((CHUNK_SIZE * RENDER_DISTANCE) / 2, 1000.0f, 0.0f);
                shaderProgram.setVec3("lightPos", lightPos);
                shaderProgram.setVec3("lightColor", lightColor);
                shaderProgram.setMat4("projection", projection);
                shaderProgram.setMat4("model", glm::mat4(1.0f));
                player.defineLookAt(shaderProgram);

                TextureArray::bind();

                if (measuring) profiler.beginUpdate();
                w->update(player.getPosition());
                if (measuring) profiler.endUpdate();

                glm::mat4 vp = projection * player.getViewMatrix();

                double t0 = glfwGetTime();
                if (measuring) profiler.beginRender();
                w->render(shaderProgram, vp, player.getPosition());
                if (measuring) profiler.endRender();

                if (measuring) profiler.beginSwap();
                if (headlessMode)
                    glFinish(); // Drain GPU pipeline without WSL2 presentation overhead
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

            std::cout << "\nBenchmark complete. Results written to benchmark_results.txt + profile_results.txt"
                      << std::endl;
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

            // Player physics + targeting
            player.update(w);

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

            glm::vec3 cameraPos = player.getPosition();

            // Sun orbits east-west (X axis) overhead, centered on camera
            glm::vec3 lightPos(cameraPos.x + cos(timeValue) * radius, sin(timeValue) * radius, cameraPos.z);
            shaderProgram.setVec3("lightPos", lightPos);
            shaderProgram.setVec3("lightColor", lightColor);

            player.defineLookAt(shaderProgram);

            glm::mat4 projection =
                glm::perspective(glm::radians(45.0f), (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
            shaderProgram.setMat4("projection", projection);

            glm::mat4 model = glm::mat4(1.0f);
            shaderProgram.setMat4("model", model);

            glm::vec2 windowSize = glm::vec2(windowWidth, windowHeight);
            shaderProgram.setVec2("windowSize", windowSize);

            // Targeting handled by player.update()

            TextureArray::bind();
            w->update(player.getPosition());
            glm::mat4 viewProjection = projection * player.getViewMatrix();

            // Render sun billboard (before terrain, no depth write)
            constexpr float SUN_DISTANCE = 800.0f;
            constexpr float SUN_SIZE = 60.0f;
            if (lightPos.y > 0) { // only when sun is above horizon
                glm::vec3 sunDir = glm::normalize(lightPos - cameraPos);
                glm::vec3 sunCenter = cameraPos + sunDir * SUN_DISTANCE;
                // Avoid degenerate cross product when sun is directly overhead
                glm::vec3 upRef = (glm::abs(sunDir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(sunDir, upRef)) * SUN_SIZE;
                glm::vec3 up = glm::normalize(glm::cross(right, sunDir)) * SUN_SIZE;

                float sunLayer = (float)TextureArray::SUN_LAYER;
                float sunVerts[40] = {
                    // pos(3) + uv(2) + normal(3) + layer(1) + ao(1)
                    sunCenter.x - right.x - up.x,
                    sunCenter.y - right.y - up.y,
                    sunCenter.z - right.z - up.z,
                    0,
                    0,
                    0,
                    0,
                    1,
                    sunLayer,
                    1,
                    sunCenter.x - right.x + up.x,
                    sunCenter.y - right.y + up.y,
                    sunCenter.z - right.z + up.z,
                    0,
                    1,
                    0,
                    0,
                    1,
                    sunLayer,
                    1,
                    sunCenter.x + right.x + up.x,
                    sunCenter.y + right.y + up.y,
                    sunCenter.z + right.z + up.z,
                    1,
                    1,
                    0,
                    0,
                    1,
                    sunLayer,
                    1,
                    sunCenter.x + right.x - up.x,
                    sunCenter.y + right.y - up.y,
                    sunCenter.z + right.z - up.z,
                    1,
                    0,
                    0,
                    0,
                    1,
                    sunLayer,
                    1,
                };

                glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sunVerts), sunVerts);

                shaderProgram.setFloat("emissive", 1.0f);
                glDepthMask(GL_FALSE);   // don't write to depth (sun is behind everything)
                glDisable(GL_CULL_FACE); // billboard visible from both sides
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

            chunksRendered = w->render(shaderProgram, viewProjection, player.getPosition());

            // Clouds: precomputed grid translated to follow camera + drift
            if (cloudIndexCount > 0) {
                constexpr float CLOUD_Y = (float)(CHUNK_HEIGHT + 30);
                constexpr int CLOUD_BLOCK_SIZE = 12;
                constexpr int CLOUD_GRID_SIZE = 128;
                constexpr float CLOUD_TILE = CLOUD_GRID_SIZE * CLOUD_BLOCK_SIZE; // 1024 blocks
                float drift = (float)glfwGetTime() * 1.5f;

                // Render a 3x3 grid of the cloud tile centered on camera for seamless tiling
                shaderProgram.setFloat("emissive", 1.0f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_CULL_FACE);

                float tileOriginX = std::floor(cameraPos.x / CLOUD_TILE) * CLOUD_TILE;
                float tileOriginZ = std::floor(cameraPos.z / CLOUD_TILE) * CLOUD_TILE;

                for (int tx = -1; tx <= 1; tx++) {
                    for (int tz = -1; tz <= 1; tz++) {
                        glm::mat4 cloudModel =
                            glm::translate(glm::mat4(1.0f), glm::vec3(tileOriginX + tx * CLOUD_TILE + drift, CLOUD_Y,
                                                                      tileOriginZ + tz * CLOUD_TILE));
                        shaderProgram.setMat4("model", cloudModel);
                        glBindVertexArray(cloudVAO);
                        glDrawElements(GL_TRIANGLES, cloudIndexCount, GL_UNSIGNED_INT, 0);
                    }
                }
                glBindVertexArray(0);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);
                shaderProgram.setFloat("emissive", 0.0f);
                shaderProgram.setMat4("model", glm::mat4(1.0f));
            }

            // Wireframe block highlight
            if (player.hasTargetedBlock()) {
                glm::ivec3 highlightBlock = player.getTargetedBlock();
                float x = (float)highlightBlock.x;
                float y = (float)highlightBlock.y;
                float z = (float)highlightBlock.z;
                // Slightly expanded cube to avoid z-fighting (0.502 instead of 0.5)
                float e = 0.502f;
                // 12 edges × 2 vertices = 24 vertices, each 10 floats
                // Using texture layer 0 (none) and ao=1, normal doesn't matter for lines
                float hlVerts[24 * 10];
                int vi = 0;
                auto addVert = [&](float vx, float vy, float vz) {
                    hlVerts[vi++] = vx;
                    hlVerts[vi++] = vy;
                    hlVerts[vi++] = vz;
                    hlVerts[vi++] = 0;
                    hlVerts[vi++] = 0; // uv
                    hlVerts[vi++] = 0;
                    hlVerts[vi++] = 0;
                    hlVerts[vi++] = 0; // normal
                    hlVerts[vi++] = 0; // layer
                    hlVerts[vi++] = 1; // ao
                };
                // 8 corners
                float cx[8] = {x - e, x - e, x + e, x + e, x - e, x - e, x + e, x + e};
                float cy[8] = {y - e, y + e, y + e, y - e, y - e, y + e, y + e, y - e};
                float cz[8] = {z - e, z - e, z - e, z - e, z + e, z + e, z + e, z + e};
                // 12 edges: bottom(0-1,1-2,2-3,3-0), top(4-5,5-6,6-7,7-4), verticals(0-4,1-5,2-6,3-7)
                int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                    {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto& edge : edges) {
                    addVert(cx[edge[0]], cy[edge[0]], cz[edge[0]]);
                    addVert(cx[edge[1]], cy[edge[1]], cz[edge[1]]);
                }

                glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(hlVerts), hlVerts);

                shaderProgram.setFloat("emissive", 1.0f);
                glDisable(GL_CULL_FACE);
                glLineWidth(2.0f);

                glBindVertexArray(highlightVAO);
                glDrawArrays(GL_LINES, 0, 24);
                glBindVertexArray(0);

                glEnable(GL_CULL_FACE);
                shaderProgram.setFloat("emissive", 0.0f);
            }

            // First-person arm
            {
                glm::mat4 armModel = player.getArmModelMatrix();

                glClear(GL_DEPTH_BUFFER_BIT);                   // arm always on top
                shaderProgram.setMat4("view", glm::mat4(1.0f)); // identity view (screen space)
                shaderProgram.setMat4("model", armModel);
                shaderProgram.setFloat("emissive", 1.0f);
                glDisable(GL_CULL_FACE);

                glBindVertexArray(armVAO);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);

                glEnable(GL_CULL_FACE);
                shaderProgram.setFloat("emissive", 0.0f);
                shaderProgram.setMat4("model", glm::mat4(1.0f));
                // Restore view matrix for next frame
                player.defineLookAt(shaderProgram);
            }

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