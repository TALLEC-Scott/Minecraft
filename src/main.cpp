#include <iostream>
#include "gl_header.h"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
#include <random>

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
#include "game_state.h"
#include "ui_renderer.h"
#include "menu.h"

// WSL2 needs GLFW_CURSOR_NORMAL as the mode param; Emscripten needs GLFW_CURSOR
#ifdef __EMSCRIPTEN__
#define CURSOR_MODE GLFW_CURSOR
#else
#define CURSOR_MODE GLFW_CURSOR_NORMAL
#endif

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

GameState currentState = GameState::MainMenu;
GameSettings gameSettings;
bool escKeyPressed = false;

// Frame state (moved to file scope for Emscripten main loop compatibility)
static GLFWwindow* g_window = nullptr;
static Shader* g_shader = nullptr;
static UIRenderer* g_uiRenderer = nullptr;
static Menu* g_menu = nullptr;
static GLuint sunVAO, sunVBO, sunEBO;
static GLuint cloudVAO, cloudVBO, cloudEBO;
static GLuint starVAO, starVBO;
static int starCount = 0;
static GLuint cloudWallVAO, cloudWallVBO, cloudWallEBO;
static int cloudIndexCount = 0;
static GLuint highlightVAO, highlightVBO;
static GLuint armVAO, armVBO, armEBO;
static GLuint heldBlockVAO, heldBlockVBO, heldBlockEBO;
static block_type lastHeldBlock = AIR; // track when to rebuild held block mesh
static double lastTime = 0, lastFrameTime = 0;
static int nbFrames = 0, chunksRendered = 0;

static void applySettings() {
    glfwSwapInterval(gameSettings.vsync ? 1 : 0);
    player.setMouseSensitivity(gameSettings.mouseSensitivity);
    if (g_menu) g_menu->setMusicVolume(gameSettings.musicVolume);
    if (w) {
        w->chunkManager->setRenderDistance(gameSettings.renderDistance);
        // Rebuild all chunk meshes if greedy meshing toggle changed
        if (g_greedyMeshing != gameSettings.greedyMeshing ||
            g_fancyLeaves != gameSettings.fancyLeaves) {
            g_greedyMeshing = gameSettings.greedyMeshing;
            g_fancyLeaves = gameSettings.fancyLeaves;
            for (auto& [pos, chunk] : w->chunkManager->chunks) chunk.markDirty();
        }
    }
}

void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    // make sure the viewport matches the new window dimensions; note that width axnd
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
    windowWidth = width;
    windowHeight = height;
}

void cursorPositionCallback(GLFWwindow* /*window*/, double xPos, double yPos) {
    if (currentState == GameState::Playing) player.updateMouseLook(xPos, yPos, windowWidth, windowHeight);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xOffset*/, double yOffset) {
    if (currentState == GameState::Playing) {
        int slot = player.getSelectedSlot();
        slot -= (int)yOffset; // scroll up = previous slot
        slot = ((slot % Player::HOTBAR_SIZE) + Player::HOTBAR_SIZE) % Player::HOTBAR_SIZE;
        player.setSelectedSlot(slot);
    }
}

glm::vec3 getSkyColor(float angle) {
    glm::vec3 noonColor(0.2f, 0.6f, 1.0f);
    glm::vec3 duskColor(0.05f, 0.05f, 0.15f);
    glm::vec3 nightColor(0.01f, 0.01f, 0.05f);

    float sunHeight = std::sin(angle);

    if (sunHeight > 0.0f) {
        float t = std::min(sunHeight * 2.0f, 1.0f);
        return glm::mix(duskColor, noonColor, t);
    } else {
        float t = std::min(-sunHeight, 1.0f);
        return glm::mix(duskColor, nightColor, t);
    }
}

// Build a small cube mesh for the held block (6 faces, per-face texture from TextureArray)
static void buildHeldBlockMesh(block_type bt) {
    float s = 0.15f; // half-size of the held block
    // Face definitions: normal direction, then 4 corner offsets (each 3 floats)
    struct Face {
        float nx, ny, nz;
        float v[4][3]; // 4 vertices
        float u[4][2]; // UVs
        int faceIdx;    // for layerForFace
    };
    Face faces[6] = {
        // Front (+Z)
        {0, 0, 1, {{-s, -s, s}, {-s, s, s}, {s, s, s}, {s, -s, s}}, {{0,0},{0,1},{1,1},{1,0}}, 0},
        // Back (-Z)
        {0, 0, -1, {{s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, -s}}, {{0,0},{0,1},{1,1},{1,0}}, 1},
        // Left (-X)
        {-1, 0, 0, {{-s, -s, -s}, {-s, s, -s}, {-s, s, s}, {-s, -s, s}}, {{0,0},{0,1},{1,1},{1,0}}, 2},
        // Right (+X)
        {1, 0, 0, {{s, -s, s}, {s, s, s}, {s, s, -s}, {s, -s, -s}}, {{0,0},{0,1},{1,1},{1,0}}, 3},
        // Top (+Y)
        {0, 1, 0, {{-s, s, s}, {-s, s, -s}, {s, s, -s}, {s, s, s}}, {{0,0},{0,1},{1,1},{1,0}}, 4},
        // Bottom (-Y)
        {0, -1, 0, {{-s, -s, -s}, {-s, -s, s}, {s, -s, s}, {s, -s, -s}}, {{0,0},{0,1},{1,1},{1,0}}, 5},
    };
    // 24 verts × 10 floats, 36 indices
    float verts[24 * 10];
    unsigned int indices[36];
    int vi = 0;
    for (int f = 0; f < 6; f++) {
        float layer = (float)TextureArray::layerForFace(bt, faces[f].faceIdx);
        for (int v = 0; v < 4; v++) {
            verts[vi++] = faces[f].v[v][0];
            verts[vi++] = faces[f].v[v][1];
            verts[vi++] = faces[f].v[v][2];
            verts[vi++] = faces[f].u[v][0];
            verts[vi++] = faces[f].u[v][1];
            verts[vi++] = faces[f].nx;
            verts[vi++] = faces[f].ny;
            verts[vi++] = faces[f].nz;
            verts[vi++] = layer;
            verts[vi++] = 1.0f; // brightness
        }
        int b = f * 4;
        indices[f * 6 + 0] = b;
        indices[f * 6 + 1] = b + 1;
        indices[f * 6 + 2] = b + 2;
        indices[f * 6 + 3] = b + 2;
        indices[f * 6 + 4] = b + 3;
        indices[f * 6 + 5] = b;
    }
    if (!heldBlockVAO) {
        glGenVertexArrays(1, &heldBlockVAO);
        glGenBuffers(1, &heldBlockVBO);
        glGenBuffers(1, &heldBlockEBO);
    }
    glBindVertexArray(heldBlockVAO);
    glBindBuffer(GL_ARRAY_BUFFER, heldBlockVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, heldBlockEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_DYNAMIC_DRAW);
    constexpr int STRIDE = 10 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
    lastHeldBlock = bt;
}

void processInput(GLFWwindow* window) {
    // Pause: ESC on desktop, TAB on web
#ifdef __EMSCRIPTEN__
    bool pauseDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
#else
    bool pauseDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
#endif
    if (pauseDown && !escKeyPressed) {
        currentState = GameState::Paused;
        glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
        escKeyPressed = pauseDown;
        return;
    }
    escKeyPressed = pauseDown;

#ifndef __EMSCRIPTEN__
    // Enable/Disable wireframe mode
    bool xKeyDown = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;
    if (xKeyDown && !xKeyPressed) {
        wireframeMode = !wireframeMode;
        if (wireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }
    xKeyPressed = xKeyDown;
#endif

    // Player input
    player.handleInput(window, w);

#ifndef __EMSCRIPTEN__
    // Enable/Disable fullscreen mode
    bool f12KeyDown = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
    if (f12KeyDown && !f12KeyPressed) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        fullscreenMode = !fullscreenMode;
        if (fullscreenMode && glfwGetWindowMonitor(window) == nullptr) {
            glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
        } else {
            glfwSetWindowMonitor(window, nullptr, 100, 100, 800, 600, GLFW_DONT_CARE);
        }
    }
    f12KeyPressed = f12KeyDown;
#endif
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

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    std::cout << "Using WebGL 2.0 (OpenGL ES 3.0)" << std::endl;
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    std::cout << "Using OpenGL 3.3 Core Profile" << std::endl;
#endif

    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "POGL", nullptr, nullptr);
    GLFWwindow* window = g_window;
    if (window == nullptr) {
        std::cout << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Window successfully created" << std::endl;
    glfwMakeContextCurrent(window);

#ifndef __EMSCRIPTEN__
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Glad successfully loaded" << std::endl;
#endif

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Start with cursor visible for main menu
    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
    glfwSetCursorPos(window, static_cast<double>(WINDOW_WIDTH) / 2.0, static_cast<double>(WINDOW_HEIGHT) / 2.0);

    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Load settings
    gameSettings.load("settings.txt");
    glfwSwapInterval(gameSettings.vsync ? 1 : 0);

#ifndef __EMSCRIPTEN__
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
#endif

    glEnable(GL_DEPTH_TEST);

    int nrAttributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
    std::cout << "Maximum number of vertex attributes supported: " << nrAttributes << std::endl;

    TextureArray::initialize();
    TextureArray::initLayerTextures();

    // Initialize UI
    // static storage on Emscripten (main never returns), auto on desktop
#ifdef __EMSCRIPTEN__
    static
#endif
        UIRenderer uiRendererObj;
    uiRendererObj.init();
    g_uiRenderer = &uiRendererObj;
#ifdef __EMSCRIPTEN__
    static
#endif
        Menu menuObj;
    menuObj.init();
    g_menu = &menuObj;

    // Share audio engine with player for footstep sounds
    player.initAudio(menuObj.getAudioEngine());
    menuObj.startMenuMusic();

    {
#ifdef __EMSCRIPTEN__
        static
#endif
            Shader shaderProgram("assets/Shaders/vert.shd", "assets/Shaders/frag.shd");
        g_shader = &shaderProgram;
#ifdef __EMSCRIPTEN__
        static
#endif
        Shader billboardShader("assets/Shaders/billboard_vert.shd", "assets/Shaders/billboard_frag.shd");
#ifdef __EMSCRIPTEN__
        static
#endif
            World world(worldSeed);
        std::cout << "World seed: " << worldSeed << std::endl;
        w = &world;

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);

        // Sun billboard quad
        glGenVertexArrays(1, &sunVAO);
        glGenBuffers(1, &sunVBO);
        glGenBuffers(1, &sunEBO);
        unsigned int sunIdx[] = {0, 1, 2, 2, 3, 0};
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIdx), sunIdx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(4 * 10) * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        constexpr int SUN_STRIDE = 10 * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, SUN_STRIDE, nullptr);
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

        // Cloud system: texture-based infinite tiling clouds with 3D volume
        constexpr int CLOUD_GRID = 128;
        constexpr int CLOUD_BLOCK = 12;
        constexpr float CLOUD_DEPTH = 4.0f;
        constexpr float CLOUD_EXTENT = 2000.0f; // half-size of cloud quad
        GLuint cloudPatternTex = 0;
#ifdef __EMSCRIPTEN__
        static
#endif
            Shader cloudShader("assets/Shaders/cloud_vert.shd", "assets/Shaders/cloud_frag.shd");
        {
            // Generate cloud pattern texture from noise
            std::vector<uint8_t> pixels(CLOUD_GRID * CLOUD_GRID * 4);
            for (int gx = 0; gx < CLOUD_GRID; gx++) {
                for (int gz = 0; gz < CLOUD_GRID; gz++) {
                    int idx = (gz * CLOUD_GRID + gx) * 4;
                    bool isCloud = world.terrainGenerator->getNoise(gx, gz) >= 0.65f;
                    pixels[idx + 0] = 255;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = isCloud ? 255 : 0;
                }
            }
            glGenTextures(1, &cloudPatternTex);
            glBindTexture(GL_TEXTURE_2D, cloudPatternTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CLOUD_GRID, CLOUD_GRID, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            // Cloud quad VAO: just 4 vertices, reused for all 6 faces
            float quadVerts[] = {
                -CLOUD_EXTENT, 0, -CLOUD_EXTENT, -CLOUD_EXTENT, 0, CLOUD_EXTENT,
                CLOUD_EXTENT,  0, CLOUD_EXTENT,  CLOUD_EXTENT,  0, -CLOUD_EXTENT,
            };
            unsigned int quadIdx[] = {0, 1, 2, 2, 3, 0};
            glGenVertexArrays(1, &cloudVAO);
            glGenBuffers(1, &cloudVBO);
            glGenBuffers(1, &cloudEBO);
            glBindVertexArray(cloudVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cloudEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);

            // Wall quad: vertical, in XY plane at Z=0
            float wallVerts[] = {
                -CLOUD_EXTENT, 0, 0, CLOUD_EXTENT, 0, 0, CLOUD_EXTENT, CLOUD_DEPTH, 0, -CLOUD_EXTENT, CLOUD_DEPTH, 0,
            };
            glGenVertexArrays(1, &cloudWallVAO);
            glGenBuffers(1, &cloudWallVBO);
            glGenBuffers(1, &cloudWallEBO);
            glBindVertexArray(cloudWallVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cloudWallVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(wallVerts), wallVerts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cloudWallEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        }

        // Wireframe highlight cube (12 edges = 24 line vertices)
        glGenVertexArrays(1, &highlightVAO);
        glGenBuffers(1, &highlightVBO);
        glBindVertexArray(highlightVAO);
        glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(24 * 10) * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        constexpr int HL_STRIDE = 10 * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, HL_STRIDE, nullptr);
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
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ARM_STRIDE, nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(5 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, ARM_STRIDE, (void*)(9 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glDisableVertexAttribArray(5);
            glVertexAttrib1f(5, 1.0f);
            glBindVertexArray(0);
        }

        // Stars: random points on a sphere, rendered as GL_POINTS at night
        {
            constexpr int NUM_STARS = 1500;
            constexpr float STAR_DIST = 500.0f;
            starCount = NUM_STARS;
            std::mt19937 starRng(12345);
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            std::uniform_real_distribution<float> bright(0.5f, 1.0f);

            // Each star: pos(3) + uv(2) + normal(3) + layer(1) + ao(1) = 10 floats
            std::vector<float> starVerts;
            starVerts.reserve(NUM_STARS * 10);
            for (int i = 0; i < NUM_STARS; i++) {
                // Uniform distribution on upper hemisphere using spherical coords
                // theta in [0, pi/2] (horizon to zenith), phi in [0, 2pi]
                std::uniform_real_distribution<float> u01(0.0f, 1.0f);
                float theta = u01(starRng) * glm::radians(120.0f); // [0, 2π/3]
                float phi = u01(starRng) * 6.2831853f;  // [0, 2pi]
                float x = std::sin(theta) * std::cos(phi) * STAR_DIST;
                float y = std::cos(theta) * STAR_DIST;  // always >= 0
                float z = std::sin(theta) * std::sin(phi) * STAR_DIST;

                float b = bright(starRng);
                float cl = (float)TextureArray::CLOUD_LAYER; // white texture
                starVerts.insert(starVerts.end(), {x, y, z, 0, 0, 0, 0, 0, cl, b});
            }

            glGenVertexArrays(1, &starVAO);
            glGenBuffers(1, &starVBO);
            glBindVertexArray(starVAO);
            glBindBuffer(GL_ARRAY_BUFFER, starVBO);
            glBufferData(GL_ARRAY_BUFFER, starVerts.size() * sizeof(float), starVerts.data(), GL_STATIC_DRAW);
            constexpr int STAR_STRIDE = 10 * sizeof(float);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STAR_STRIDE, nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STAR_STRIDE, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STAR_STRIDE, (void*)(5 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STAR_STRIDE, (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STAR_STRIDE, (void*)(9 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glDisableVertexAttribArray(5);
            glVertexAttrib1f(5, 1.0f);
            glBindVertexArray(0);
        }

        lastTime = glfwGetTime();
        lastFrameTime = lastTime;
        nbFrames = 0;
        chunksRendered = 0;

#ifndef __EMSCRIPTEN__
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

            glm::mat4 projection = glm::perspective(glm::radians(gameSettings.fov),
                                                    (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
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
                    // Phase 2 (measured): move forward at fixed sprint speed
                    player.getCamera().changeDirection(glm::vec3(1.0f, 0.0f, 0.0f));
                    player.getCamera().setSpeed(3 * SPEED); // fixed benchmark speed
                    player.getCamera().forward();
                }

                glm::vec3 benchSky = getSkyColor(0.0f);
                glClearColor(benchSky.r, benchSky.g, benchSky.b, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                shaderProgram.use();

                glm::vec3 lightPos((CHUNK_SIZE * RENDER_DISTANCE) / 2, 1000.0f, 0.0f);
                glm::vec3 sunDir = glm::normalize(lightPos);
                shaderProgram.setVec3("sunDir", sunDir);
                shaderProgram.setVec3("lightColor", lightColor);
                shaderProgram.setMat4("projection", projection);
                shaderProgram.setMat4("model", glm::mat4(1.0f));
                player.defineLookAt(shaderProgram);

                // Fog uniforms for benchmark
                float bFogEnd = (float)(gameSettings.renderDistance * CHUNK_SIZE);
                shaderProgram.setVec3("cameraPos", player.getPosition());
                shaderProgram.setFloat("fogStart", bFogEnd * 0.6f);
                shaderProgram.setFloat("fogEnd", bFogEnd);
                shaderProgram.setVec3("fogColor", benchSky);

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
#endif // !__EMSCRIPTEN__

        // Apply initial settings
        player.setMouseSensitivity(gameSettings.mouseSensitivity);
        w->chunkManager->setRenderDistance(gameSettings.renderDistance);

        // Extract loop body into a lambda for Emscripten compatibility
        auto mainLoopBody = [&]() {
            GLFWwindow* window = g_window;
            Shader& shaderProgram = *g_shader;
            UIRenderer& uiRenderer = *g_uiRenderer;
            Menu& menu = *g_menu;

            // --- Menu states ---
            if (currentState == GameState::MainMenu) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                GameState next = menu.drawMainMenu(uiRenderer, windowWidth, windowHeight, window);
                if (next == GameState::Playing && currentState != GameState::Playing) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                    menu.startMusic();
                }
                if (next == GameState::Settings) applySettings();
                currentState = next;
                glfwSwapBuffers(window);
                glfwPollEvents();
                return;
            }

            if (currentState == GameState::Settings) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                GameState next = menu.drawSettings(uiRenderer, windowWidth, windowHeight, window, gameSettings);
                if (next != GameState::Settings) {
                    gameSettings.save("settings.txt");
                    applySettings();
                    if (next == GameState::Playing) {
                        glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                        player.resetMouseState();
                        player.consumeMouseButtons();
                    }
                }
                currentState = next;
                glfwSwapBuffers(window);
                glfwPollEvents();
                return;
            }

            // --- Game rendering (used by both Playing and Paused) ---
            float speed = 0.025;
            float timeValue = 0.0f;
            if (doDaylightCycle) {
                timeValue = glfwGetTime() * speed;
            }
            float radius = 1000.0f;

            glm::vec3 skyColor = getSkyColor(timeValue);
            glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Delta time — computed before player update so movement is frame-rate independent
            double currentTime = glfwGetTime();
            float dt = (float)(currentTime - lastFrameTime);
            if (dt > 0.1f) dt = 0.1f;
            player.getCamera().setDeltaTime(dt);
            double frameTime = dt * 1000.0;
            lastFrameTime = currentTime;

            if (currentState == GameState::Playing) {
                player.update(w);
            }
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

            if (currentState == GameState::Playing) {
                processInput(window);
            }
            shaderProgram.use();

            glm::vec3 cameraPos = player.getPosition();

            // Sun orbits east-west (X axis) overhead, centered on camera
            glm::vec3 lightPos(cameraPos.x + std::cos(timeValue) * radius, std::sin(timeValue) * radius, cameraPos.z);
            glm::vec3 sunDir = glm::normalize(lightPos - cameraPos);
            shaderProgram.setVec3("sunDir", sunDir);

            // Light color: warm orange near horizon, white at noon, fades to dark at night
            float sunH = std::max(sunDir.y, 0.0f);
            glm::vec3 sunColor = glm::vec3(1.0f) * std::min(sunH * 1.5f, 1.0f);
            shaderProgram.setVec3("lightColor", sunColor);

            player.defineLookAt(shaderProgram);

            glm::mat4 projection = glm::perspective(glm::radians(gameSettings.fov),
                                                    (float)windowWidth / (float)windowHeight, 0.1f, 5000.0f);
            shaderProgram.setMat4("projection", projection);

            glm::mat4 model = glm::mat4(1.0f);
            shaderProgram.setMat4("model", model);

            glm::vec2 windowSize = glm::vec2(windowWidth, windowHeight);
            shaderProgram.setVec2("windowSize", windowSize);

            // Fog: blend to sky color at render distance edges
            float fogEnd = (float)(gameSettings.renderDistance * CHUNK_SIZE);
            float fogStart = fogEnd * 0.6f;
            shaderProgram.setVec3("cameraPos", cameraPos);
            shaderProgram.setFloat("fogStart", fogStart);
            shaderProgram.setFloat("fogEnd", fogEnd);
            shaderProgram.setVec3("fogColor", skyColor);
            float gameTime = (float)glfwGetTime();
            shaderProgram.setFloat("time", gameTime);
            shaderProgram.setInt("fancyLeaves", g_fancyLeaves ? 1 : 0);
            shaderProgram.setVec2("leafSway", glm::vec2(std::sin(gameTime * 2.0f) * 0.06f,
                                                         std::cos(gameTime * 1.5f) * 0.04f));

            // Targeting handled by player.update()

            TextureArray::bind();
            if (currentState == GameState::Playing) {
                w->update(player.getPosition());
            }
            glm::mat4 viewProjection = projection * player.getViewMatrix();

            // --- Billboard rendering (separate shader, no terrain features) ---
            billboardShader.use();
            billboardShader.setMat4("projection", projection);
            billboardShader.setMat4("view", player.getViewMatrix());
            billboardShader.setVec3("tintColor", glm::vec3(1.0f));
            TextureArray::bind();

            // Stars
            if (sunH < 0.1f) {
                float starAlpha = 1.0f - sunH / 0.1f;
                float starRotation = timeValue * 0.3f;

                glm::mat4 starModel = glm::translate(glm::mat4(1.0f), cameraPos);
                starModel = glm::rotate(starModel, starRotation, glm::vec3(0.0f, 1.0f, 0.0f));

                billboardShader.setMat4("model", starModel);
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
#ifndef __EMSCRIPTEN__
                glPointSize(2.0f);
#endif

                glBindVertexArray(starVAO);
                glDrawArrays(GL_POINTS, 0, starCount);
                glBindVertexArray(0);

                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
            }

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

                billboardShader.setMat4("model", glm::mat4(1.0f));
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                glBindVertexArray(sunVAO);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);

                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);
            }

            // Render moon billboard (opposite side of sun)
            if (lightPos.y < 200.0f) {                                            // moon rises before sun fully sets
                glm::vec3 moonDir = glm::normalize(-lightPos + 2.0f * cameraPos); // opposite of sun
                glm::vec3 moonCenter = cameraPos + moonDir * SUN_DISTANCE;
                constexpr float MOON_SIZE = 45.0f;
                glm::vec3 upRef = (glm::abs(moonDir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                glm::vec3 right = glm::normalize(glm::cross(moonDir, upRef)) * MOON_SIZE;
                glm::vec3 up = glm::normalize(glm::cross(right, moonDir)) * MOON_SIZE;

                float moonLayer = (float)TextureArray::MOON_LAYER;
                float moonVerts[40] = {
                    moonCenter.x - right.x - up.x,
                    moonCenter.y - right.y - up.y,
                    moonCenter.z - right.z - up.z,
                    0,
                    0,
                    0,
                    0,
                    1,
                    moonLayer,
                    1,
                    moonCenter.x - right.x + up.x,
                    moonCenter.y - right.y + up.y,
                    moonCenter.z - right.z + up.z,
                    0,
                    1,
                    0,
                    0,
                    1,
                    moonLayer,
                    1,
                    moonCenter.x + right.x + up.x,
                    moonCenter.y + right.y + up.y,
                    moonCenter.z + right.z + up.z,
                    1,
                    1,
                    0,
                    0,
                    1,
                    moonLayer,
                    1,
                    moonCenter.x + right.x - up.x,
                    moonCenter.y + right.y - up.y,
                    moonCenter.z + right.z - up.z,
                    1,
                    0,
                    0,
                    0,
                    1,
                    moonLayer,
                    1,
                };

                glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(moonVerts), moonVerts);

                billboardShader.setMat4("model", glm::mat4(1.0f));
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                glBindVertexArray(sunVAO);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);

                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);
            }

            // Restore world shader after billboards
            shaderProgram.use();
            TextureArray::bind();
            chunksRendered = w->render(shaderProgram, viewProjection, player.getPosition());

            // Clouds: texture-based infinite tiling with 3D volume
            {
                constexpr float CLOUD_Y_TOP = (float)(CHUNK_HEIGHT + 30) + CLOUD_DEPTH;
                constexpr float CLOUD_Y_BOT = (float)(CHUNK_HEIGHT + 30);
                float drift = (float)glfwGetTime() * 1.5f;
                float cloudTileSize = (float)(CLOUD_GRID * CLOUD_BLOCK);

                cloudShader.use();
                cloudShader.setMat4("projection", projection);
                cloudShader.setMat4("view", player.getViewMatrix());
                cloudShader.setFloat("cloudScale", 1.0f / cloudTileSize);
                cloudShader.setFloat("drift", drift);
                cloudShader.setVec3("fogColor", skyColor);
                cloudShader.setVec3("cameraPos", cameraPos);
                cloudShader.setFloat("fogStart", fogStart);
                cloudShader.setFloat("fogEnd", fogEnd);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, cloudPatternTex);
                cloudShader.setInt("cloudPattern", 0);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_CULL_FACE);
                glDepthMask(GL_FALSE);

                // Pass cloud slab bounds for raymarching
                cloudShader.setFloat("cloudBottom", CLOUD_Y_BOT);
                cloudShader.setFloat("cloudTop", CLOUD_Y_TOP);

                glBindVertexArray(cloudVAO);

                // Single quad at cloud level — fragment shader raymarches through slab
                glm::mat4 cloudModel = glm::translate(glm::mat4(1.0f), glm::vec3(cameraPos.x, CLOUD_Y_TOP, cameraPos.z));
                cloudShader.setMat4("model", cloudModel);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

                glBindVertexArray(0);

                glBindVertexArray(0);
                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);

                // Restore world shader
                shaderProgram.use();
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
                    hlVerts[vi++] = (float)TextureArray::CLOUD_LAYER; // white
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

                billboardShader.use();
                billboardShader.setMat4("projection", projection);
                billboardShader.setMat4("view", player.getViewMatrix());
                billboardShader.setMat4("model", glm::mat4(1.0f));
                billboardShader.setVec3("tintColor", glm::vec3(0.02f));
                glDisable(GL_CULL_FACE);
                glLineWidth(2.0f);

                glBindVertexArray(highlightVAO);
                glDrawArrays(GL_LINES, 0, 24);
                glBindVertexArray(0);

                glEnable(GL_CULL_FACE);
            }

            // First-person arm — lit by environment (matches terrain shader)
            {
                glm::mat4 armModel = player.getArmModelMatrix();

                // Sample sky light at player eye position
                glm::vec3 armTint(1.0f);
                {
                    int bx = (int)std::floor(cameraPos.x + 0.5f);
                    int bz = (int)std::floor(cameraPos.z + 0.5f);
                    int by = (int)std::floor(cameraPos.y + 0.5f);
                    int cx = worldToChunk(bx);
                    int cz = worldToChunk(bz);
                    Chunk* chunk = w->chunkManager->getChunk(cx, cz);
                    float skyFactor = 1.0f;
                    if (chunk) {
                        uint8_t sl = chunk->getSkyLight(worldToLocal(bx, cx), by, worldToLocal(bz, cz));
                        skyFactor = 0.15f + 0.85f * (sl / 15.0f);
                    }
                    // Sun contribution (matches frag.shd: sunContrib * lightColor)
                    float sunContrib = std::min(0.35f * sunH + 0.65f * sunH, 1.0f);
                    glm::vec3 sunLit = sunColor * sunContrib * skyFactor;
                    // Moon contribution (matches frag.shd: 0.25 * moonIntensity * blue tint)
                    float moonIntensity = std::max(-sunDir.y, 0.0f);
                    glm::vec3 moonLit = glm::vec3(0.6f, 0.7f, 1.0f) * 0.25f * moonIntensity * skyFactor;
                    // Base ambient
                    armTint = sunLit + moonLit + glm::vec3(0.08f);
                }

                glClear(GL_DEPTH_BUFFER_BIT);
                billboardShader.use();
                billboardShader.setMat4("projection", projection);
                billboardShader.setMat4("view", glm::mat4(1.0f));
                billboardShader.setVec3("tintColor", armTint);

                block_type heldType = player.getSelectedBlockType();
                glDisable(GL_CULL_FACE);
                if (heldType == AIR) {
                    // Empty hand: show arm
                    billboardShader.setMat4("model", armModel);
                    glBindVertexArray(armVAO);
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
                } else {
                    // Held block: smaller cube, tilted, in hand position
                    if (heldType != lastHeldBlock) buildHeldBlockMesh(heldType);
                    glm::mat4 blockModel = glm::mat4(1.0f);
                    blockModel = glm::translate(blockModel, glm::vec3(0.4f, -0.35f, -0.5f));
                    blockModel = glm::rotate(blockModel, glm::radians(player.getPunchSwingAngle()), glm::vec3(1, 0, 0));
                    blockModel = glm::rotate(blockModel, glm::radians(25.0f), glm::vec3(0, 1, 0));
                    blockModel = glm::rotate(blockModel, glm::radians(10.0f), glm::vec3(1, 0, 0));
                    billboardShader.setMat4("model", blockModel);
                    TextureArray::bind();
                    glBindVertexArray(heldBlockVAO);
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
                }
                glBindVertexArray(0);
                glEnable(GL_CULL_FACE);
            }

            // Crosshair (Minecraft-style: inverted colors, centered cross)
            if (currentState == GameState::Playing) {
                float cx = std::floor(windowWidth / 2.0f);
                float cy = std::floor(windowHeight / 2.0f);
                constexpr float arm = 8.0f;  // arm length
                constexpr float t = 2.0f;    // thickness
                float ht = t / 2.0f;
                uiRenderer.begin(windowWidth, windowHeight);
                glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
                // Vertical bar
                uiRenderer.drawRect(cx - ht, cy - arm, t, arm * 2, glm::vec4(1.0f));
                // Horizontal bar (exclude center overlap)
                uiRenderer.drawRect(cx - arm, cy - ht, arm - ht, t, glm::vec4(1.0f));
                uiRenderer.drawRect(cx + ht, cy - ht, arm - ht, t, glm::vec4(1.0f));
                uiRenderer.end();
            }

            // Hotbar
            if (currentState == GameState::Playing) {
                constexpr float SLOT_SIZE = 40.0f;
                constexpr float SLOT_PAD = 2.0f;
                constexpr float BAR_PAD = 4.0f;
                constexpr int HSIZE = Player::HOTBAR_SIZE;
                float totalW = HSIZE * SLOT_SIZE + (HSIZE - 1) * SLOT_PAD + BAR_PAD * 2;
                float barX = (windowWidth - totalW) / 2.0f;
                float barY = windowHeight - SLOT_SIZE - BAR_PAD * 2 - 10.0f;

                uiRenderer.begin(windowWidth, windowHeight);
                // Background
                uiRenderer.drawRect(barX, barY, totalW, SLOT_SIZE + BAR_PAD * 2,
                                    glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
                const block_type* hotbar = player.getHotbar();
                int sel = player.getSelectedSlot();
                for (int i = 0; i < HSIZE; i++) {
                    float sx = barX + BAR_PAD + i * (SLOT_SIZE + SLOT_PAD);
                    float sy = barY + BAR_PAD;
                    // Slot background
                    glm::vec4 slotColor = (i == sel) ? glm::vec4(1.0f, 1.0f, 1.0f, 0.4f)
                                                     : glm::vec4(0.3f, 0.3f, 0.3f, 0.4f);
                    uiRenderer.drawRect(sx, sy, SLOT_SIZE, SLOT_SIZE, slotColor);
                    // Block icon (top face) — skip empty slots
                    if (hotbar[i] != AIR) {
                        int layer = TextureArray::layerForFace(hotbar[i], 4);
                        GLuint tex = TextureArray::getLayerTexture2D(layer);
                        if (tex) {
                            uiRenderer.drawTexturedRect(sx + 4, sy + 4, SLOT_SIZE - 8, SLOT_SIZE - 8, tex, 0, 0, 1, 1);
                        }
                    }
                    // Slot number
                    std::string num = std::to_string((i + 1) % 10);
                    uiRenderer.drawTextShadow(num, sx + 2, sy + 2, 1.0f);
                }
                uiRenderer.end();
            }

            // Pause menu overlay (rendered after the world)
            if (currentState == GameState::Paused) {
                GameState next = menu.drawPauseMenu(uiRenderer, windowWidth, windowHeight, window);
                if (next == GameState::Playing) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                }
                if (next == GameState::Settings) {
                    gameSettings.save("settings.txt");
                    applySettings();
                }
                if (next == GameState::MainMenu) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
                    menu.stopMusic();
                    menu.startMenuMusic();
                }
                currentState = next;
            }

            glfwSwapBuffers(window);
            glfwPollEvents();
        }; // end mainLoopBody lambda

#ifdef __EMSCRIPTEN__
        // Wrap the lambda for emscripten_set_main_loop_arg
        static auto loopRef = mainLoopBody;
        emscripten_set_main_loop_arg([](void*) { loopRef(); }, nullptr, 0, true);
    } // close scope block (emscripten_set_main_loop_arg never returns)
    return 0;
}
#else
        while (!glfwWindowShouldClose(window)) {
            mainLoopBody();
        }

        shaderProgram.destroy();
    }                      // world and shaderProgram destroyed here, while GL context is still valid
    player.destroyAudio(); // before menu destroys the audio engine
    g_menu->destroy();
    g_uiRenderer->destroy();
cleanup:
    if (headlessFBO) {
        glDeleteFramebuffers(1, &headlessFBO);
        glDeleteTextures(1, &headlessColor);
        glDeleteRenderbuffers(1, &headlessDepth);
    }
    TextureArray::destroyLayerTextures();
    TextureArray::destroy();

    glfwDestroyWindow(window);
    std::cout << "Window destroyed" << std::endl;
    glfwTerminate();
    std::cout << "GLFW terminated" << std::endl;
    return 0;
}
#endif