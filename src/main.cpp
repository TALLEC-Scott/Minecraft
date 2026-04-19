#include <iostream>
#include "gl_header.h"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#ifdef _WIN32
// SetCurrentProcessExplicitAppUserModelID lives in shell32 since Windows 7.
// Request the header by bumping _WIN32_WINNT before including windows.h.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shobjidl.h>
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
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <random>
#include <thread>

#include "profiler.h"
#include "tracy_shim.h"

#include "shader.h"
#include "texture.h"
#include "cube.h"
#include "chunk.h"
#include "camera.h"
#include "world.h"
#include "ChunkManager.h"
#include "TerrainGenerator.h"
#include "texture_array.h"
#include "player.h"
#include "game_state.h"
#include "ui_renderer.h"
#include "menu.h"
#include "inventory.h"
#include "world_save.h"
#include "world_directory.h"
#include "entity_cube_renderer.h"
#include "entity_manager.h"
#include "particle_system.h"
#include "player_renderer.h"
#include "net/net_session.h"
#include "net/multiplayer_menu.h"

#define CURSOR_MODE GLFW_CURSOR

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 1000

int windowWidth = WINDOW_WIDTH;
int windowHeight = WINDOW_HEIGHT;

// On web the settings file has to live under /saves/ (the IDBFS-mounted
// directory), otherwise it sits in MEMFS and is wiped on page reload.
// Desktop keeps it next to the executable for easy manual editing.
#ifdef __EMSCRIPTEN__
constexpr const char* SETTINGS_PATH = "saves/settings.txt";
#else
constexpr const char* SETTINGS_PATH = "settings.txt";
#endif

// Fallback window size used when "Auto" is selected but neither the
// browser viewport nor the desktop monitor can report a usable size.
constexpr int FALLBACK_WINDOW_WIDTH = 1280;
constexpr int FALLBACK_WINDOW_HEIGHT = 720;

// Cap the frame rate to ~60 FPS in menu / loading / pause states so we don't
// burn GPU cycles when nothing in the world is changing. Uses the idle slack
// just before the swap+poll at the bottom of each menu handler. Returns how
// long the frame should wait — 0 if we've already blown the budget.
static double menuFrameBudgetSeconds() {
#ifdef __EMSCRIPTEN__
    // Browser rAF already paces the main loop; any extra wait would only
    // pile up in the event queue.
    return 0.0;
#else
    using Clock = std::chrono::steady_clock;
    static auto lastFrame = Clock::now();
    constexpr std::chrono::microseconds TARGET{16667}; // ~1/60 s
    auto now = Clock::now();
    auto elapsed = now - lastFrame;
    lastFrame = now;
    if (elapsed >= TARGET) return 0.0;
    auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(TARGET - elapsed);
    return remaining.count() / 1'000'000.0;
#endif
}

// Flush MEMFS → IndexedDB on web so anything we just wrote survives a
// page reload. No-op on desktop.
static void syncToPersistentStorage() {
#ifdef __EMSCRIPTEN__
    EM_ASM(FS.syncfs(false, function(err) {
        if (err) console.error('IDBFS sync error:', err);
    }););
#endif
}

Player player;
World* w = nullptr;

// Loading-state tracking: the chunk the player will spawn in, plus a start
// time used to render progress (and enforce a safety timeout).
int g_loadingSpawnCx = 0;
int g_loadingSpawnCz = 0;
double g_loadingStartTime = 0.0;
std::string g_loadingWorldName;

bool xKeyPressed = false;
bool wireframeMode = false;

bool f12KeyPressed = false;
bool fullscreenMode = false;

bool doDaylightCycle = true;
bool previousDaylight = false;

GameState currentState = GameState::MainMenu;
GameSettings gameSettings;
bool escKeyPressed = false;
Inventory inventory;
bool eKeyPressed = false;

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
        if (g_greedyMeshing != gameSettings.greedyMeshing || g_fancyLeaves != gameSettings.fancyLeaves) {
            g_greedyMeshing = gameSettings.greedyMeshing;
            g_fancyLeaves = gameSettings.fancyLeaves;
            for (auto& [pos, chunk] : w->chunkManager->chunks) chunk.markDirty();
        }
    }
    // Only apply resolution while windowed - fullscreen keeps its
    // monitor-native size and the preset takes effect on exit.
    if (g_window && glfwGetWindowMonitor(g_window) == nullptr) {
        const ResolutionPreset& p = RESOLUTION_PRESETS[gameSettings.resolutionIndex];
        int tw = p.width, th = p.height;
        if (tw == 0 || th == 0) {
            // "Auto": full browser viewport on web (NOT the current
            // canvas size, which would never shrink), or 80% of the
            // primary monitor's video mode on desktop.
#ifdef __EMSCRIPTEN__
            tw = EM_ASM_INT({ return window.innerWidth; });
            th = EM_ASM_INT({ return window.innerHeight; });
#else
            if (const GLFWvidmode* vm = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
                tw = static_cast<int>(vm->width * 0.8f);
                th = static_cast<int>(vm->height * 0.8f);
            }
#endif
            if (tw <= 0 || th <= 0) {
                tw = FALLBACK_WINDOW_WIDTH;
                th = FALLBACK_WINDOW_HEIGHT;
            }
        }
        if (tw != windowWidth || th != windowHeight) glfwSetWindowSize(g_window, tw, th);
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
    if (currentState == GameState::Playing && !inventory.isOpen())
        player.updateMouseLook(xPos, yPos, windowWidth, windowHeight);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xOffset*/, double yOffset) {
    if (currentState == GameState::Playing && !inventory.isOpen()) {
        int slot = player.getSelectedSlot();
        slot -= (int)yOffset; // scroll up = previous slot
        slot = ((slot % Player::HOTBAR_SIZE) + Player::HOTBAR_SIZE) % Player::HOTBAR_SIZE;
        player.setSelectedSlot(slot);
    }
}

void charCallback(GLFWwindow* /*window*/, unsigned int codepoint) {
    if (g_menu) g_menu->onCharInput(codepoint);
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    // Forward to menu for active text inputs — menu gates by whether an input is active.
    if (g_menu) g_menu->onKeyInput(window, key, action, mods);
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
        int faceIdx;   // for layerForFace
    };
    Face faces[6] = {
        // Front (+Z)
        {0, 0, 1, {{-s, -s, s}, {-s, s, s}, {s, s, s}, {s, -s, s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 0},
        // Back (-Z)
        {0, 0, -1, {{s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, -s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 1},
        // Left (-X)
        {-1, 0, 0, {{-s, -s, -s}, {-s, s, -s}, {-s, s, s}, {-s, -s, s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 2},
        // Right (+X)
        {1, 0, 0, {{s, -s, s}, {s, s, s}, {s, s, -s}, {s, -s, -s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 3},
        // Top (+Y)
        {0, 1, 0, {{-s, s, s}, {-s, s, -s}, {s, s, -s}, {s, s, s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 4},
        // Bottom (-Y)
        {0, -1, 0, {{-s, -s, -s}, {-s, -s, s}, {s, -s, s}, {s, -s, -s}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 5},
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
    // Pause: ESC on desktop, M on web (ESC exits pointer lock in browsers
    // and TAB is intercepted by tab-stop focus cycling).
#ifdef __EMSCRIPTEN__
    bool pauseDown = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
#else
    bool pauseDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
#endif
    if (pauseDown && !escKeyPressed) {
        if (inventory.isOpen()) {
            // Close inventory instead of pausing
            inventory.close();
            glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
            player.resetMouseState();
        } else {
            currentState = GameState::Paused;
            glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
            if (g_menu) {
                g_menu->stopMusic();
                g_menu->notifyEscHeldForPause();
            }
        }
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

    // Inventory toggle: 'E' key (edge-triggered)
    bool eKeyDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (eKeyDown && !eKeyPressed) {
        inventory.toggle();
        if (inventory.isOpen()) {
            glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
        } else {
            glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
            player.resetMouseState();
        }
    }
    eKeyPressed = eKeyDown;

    // Skip player input while inventory is open
    if (inventory.isOpen()) return;

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
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
#else
    bool benchmarkMode = false;
    bool headlessMode = false;
    bool stressWater = false;
    unsigned int worldSeed = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--benchmark")
            benchmarkMode = true;
        else if (arg == "--headless")
            headlessMode = true;
        else if (arg == "--stress-water")
            stressWater = true;
        else if (arg == "--seed" && i + 1 < argc)
            worldSeed = std::stoul(argv[++i]);
    }
#endif
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
#ifdef _WIN32
    // Register a stable AppUserModelID so the Windows shell treats this as
    // a distinct app (otherwise the taskbar button groups under a generic
    // "unassigned" owner that ignores the per-window icon). Resolved
    // dynamically because mingw-w64's shobjidl.h doesn't always
    // forward-declare it.
    {
        static constexpr PCWSTR APP_USER_MODEL_ID = L"TALLEC.Minecraft.POGL";
        using SetAppIDFn = HRESULT(WINAPI*)(PCWSTR);
        // shell32 is always loaded — use GetModuleHandle so we don't bump
        // the DLL refcount and need a matching FreeLibrary.
        if (HMODULE shell32 = GetModuleHandleW(L"shell32.dll")) {
            FARPROC raw = GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID");
            auto setAppID = reinterpret_cast<SetAppIDFn>(reinterpret_cast<void*>(raw));
            if (setAppID) setAppID(APP_USER_MODEL_ID);
        }
    }
#endif
    {
        static constexpr const char* ICON_PATHS[] = {
            "assets/minecraft_icon_16.png",
            "assets/minecraft_icon_32.png",
            "assets/minecraft_icon_48.png",
            "assets/minecraft_icon_64.png",
        };
        constexpr int ICON_COUNT = sizeof(ICON_PATHS) / sizeof(ICON_PATHS[0]);
        GLFWimage images[ICON_COUNT];
        unsigned char* pixels[ICON_COUNT] = {};
        int count = 0;
        for (int i = 0; i < ICON_COUNT; ++i) {
            int iw, ih, ichan;
            pixels[i] = stbi_load(ICON_PATHS[i], &iw, &ih, &ichan, 4);
            if (!pixels[i]) continue;
            images[count].width = iw;
            images[count].height = ih;
            images[count].pixels = pixels[i];
            ++count;
        }
        if (count > 0) glfwSetWindowIcon(window, count, images);
        for (auto* p : pixels)
            if (p) stbi_image_free(p);
        // GLFW issue #2753: Windows drops the taskbar icon update if
        // glfwPollEvents isn't called within ~500 ms of setWindowIcon,
        // and our init easily exceeds that. Pump once here.
        glfwPollEvents();
    }
#endif

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
    glfwSetCharCallback(window, charCallback);
    glfwSetKeyCallback(window, keyCallback);

    // Load settings and apply them so the resolution preset + vsync are
    // honoured from the very first frame (otherwise "Auto" on web stays
    // at the default canvas size until the user opens the Settings menu).
    gameSettings.load(SETTINGS_PATH);
    applySettings();

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
        // World / WorldSave are optional so we can tear down one world and
        // load another mid-run (when the user picks or creates a world in
        // the singleplayer menu). Static on Emscripten since main() never
        // returns; plain auto on desktop.
        WorldSave::mountPersistentStorage();
#ifdef __EMSCRIPTEN__
        static
#endif
            std::optional<WorldSave> worldSave;
#ifdef __EMSCRIPTEN__
        static
#endif
            std::optional<World> world;
#ifndef __EMSCRIPTEN__
        bool skipPlayerRestore = benchmarkMode;
#else
        bool skipPlayerRestore = false;
#endif

#ifndef __EMSCRIPTEN__
        // Benchmark-only default-world picker: most-recent in saves/, else
        // legacy "world", else a fresh "World" folder. Interactive runs use
        // the Singleplayer menu instead.
        auto pickInitialFolder = []() -> std::string {
            auto all = listWorlds();
            if (!all.empty()) return all[0].folder;
            if (std::filesystem::exists("saves/world/level.dat")) return "world";
            return uniqueFolderName("World");
        };
#endif

        auto captureCurrentPlayer = [&]() {
            PlayerSaveData pd;
            pd.position = player.getPosition();
            pd.yaw = player.getYaw();
            pd.pitch = player.getPitch();
            pd.walkMode = player.isWalkMode();
            std::memcpy(pd.hotbar, player.getHotbar(), sizeof(pd.hotbar));
            pd.selectedSlot = player.getSelectedSlot();
            return pd;
        };

        auto saveCurrentWorld = [&]() -> bool {
            if (!world || !worldSave) return false;
            world->chunkManager->saveAllModifiedChunks();
            worldSave->saveLevelData(world->getSeed(), captureCurrentPlayer());
            WorldSave::syncToDisk();
            return true;
        };

        // Cloud pattern texture. Regenerated each world switch so the cloud
        // layout tracks the world's seed (pre-refactor behavior). VAO setup
        // happens later; the texture object is created lazily on first use.
        constexpr int CLOUD_GRID = 128;
        constexpr int CLOUD_BLOCK = 12;
        constexpr float CLOUD_DEPTH = 4.0f;
        constexpr float CLOUD_EXTENT = 2000.0f;
        GLuint cloudPatternTex = 0;
        // clouds.bin format: 4-byte magic 'CLDP' | 1-byte version | 2048 bytes
        // packed bitmask (128 × 128 bits, LSB = cloud-present). Unpacks back
        // to RGBA at upload time since the shader expects an alpha texture —
        // the packed form is just for on-disk compactness (2053 B vs 64 KB).
        constexpr uint32_t CLOUD_MAGIC = 0x50444C43;
        constexpr uint8_t CLOUD_VERSION = 1;
        constexpr size_t CLOUD_BITS = static_cast<size_t>(CLOUD_GRID) * CLOUD_GRID;
        constexpr size_t CLOUD_PACKED_BYTES = (CLOUD_BITS + 7) / 8;

        auto uploadCloudBits = [&](const std::vector<uint8_t>& packed) {
            std::vector<uint8_t> pixels(CLOUD_BITS * 4);
            for (size_t i = 0; i < CLOUD_BITS; ++i) {
                bool cloud = (packed[i >> 3] >> (i & 7)) & 1;
                pixels[i * 4 + 0] = 255;
                pixels[i * 4 + 1] = 255;
                pixels[i * 4 + 2] = 255;
                pixels[i * 4 + 3] = cloud ? 255 : 0;
            }
            if (!cloudPatternTex) glGenTextures(1, &cloudPatternTex);
            glBindTexture(GL_TEXTURE_2D, cloudPatternTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CLOUD_GRID, CLOUD_GRID, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        };
        auto bakeCloudBits = [&](unsigned int seed) {
            TerrainGenerator cloudNoise(seed, 0.1f, 0, 10);
            std::vector<uint8_t> packed(CLOUD_PACKED_BYTES, 0);
            for (int gx = 0; gx < CLOUD_GRID; gx++) {
                for (int gz = 0; gz < CLOUD_GRID; gz++) {
                    if (cloudNoise.getNoise(gx, gz) >= 0.65f) {
                        size_t bit = static_cast<size_t>(gz) * CLOUD_GRID + gx;
                        packed[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
                    }
                }
            }
            return packed;
        };
        // Load packed cloud bits from saves/<folder>/clouds.bin if present,
        // else bake from the world's seed and persist so the next load is a
        // straight file read. Files from previous RGBA-blob format lack the
        // CLDP magic and get silently re-baked into the new format.
        auto loadOrBakeCloudPattern = [&](const std::string& folder, unsigned int seed) {
            std::string path = "saves/" + folder + "/clouds.bin";
            std::vector<uint8_t> packed;
            {
                std::ifstream in(path, std::ios::binary);
                if (in) {
                    uint32_t magic = 0;
                    uint8_t version = 0;
                    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
                    in.read(reinterpret_cast<char*>(&version), sizeof(version));
                    if (magic == CLOUD_MAGIC && version == CLOUD_VERSION) {
                        packed.resize(CLOUD_PACKED_BYTES);
                        in.read(reinterpret_cast<char*>(packed.data()), CLOUD_PACKED_BYTES);
                        if (in.gcount() != static_cast<std::streamsize>(CLOUD_PACKED_BYTES)) packed.clear();
                    }
                }
            }
            if (packed.empty()) {
                packed = bakeCloudBits(seed);
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                if (out) {
                    uint32_t magic = CLOUD_MAGIC;
                    uint8_t version = CLOUD_VERSION;
                    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
                    out.write(reinterpret_cast<const char*>(packed.data()), CLOUD_PACKED_BYTES);
                }
            }
            uploadCloudBits(packed);
        };

        // Load (or create) the world at saves/<folder>. `seedOverride` forces
        // a specific seed (new-world creation or --seed); otherwise level.dat
        // wins when present.
        auto switchToWorld = [&](const std::string& folder, std::optional<unsigned int> seedOverride,
                                 const std::string& displayName) {
            saveCurrentWorld();
            world.reset();
            worldSave.reset();
            w = nullptr;

            std::filesystem::create_directories("saves/" + folder + "/chunks");
            worldSave.emplace("saves/" + folder);

            unsigned int seed = seedOverride.value_or(0);
            PlayerSaveData loaded;
            bool hadSave = worldSave->loadLevelData(seed, loaded);
            if (seedOverride.has_value()) seed = *seedOverride;  // creation overrides stored seed
            if (!displayName.empty()) worldSave->setDisplayName(displayName);

            std::cout << "World: " << folder << "  seed: " << seed << std::endl;
            loadOrBakeCloudPattern(folder, seed);
            world.emplace(seed);
            world->chunkManager->setWorldSave(&*worldSave);
            world->waterSimulator->initAudio(menuObj.getAudioEngine());
            world->entityManager->initAudio(menuObj.getAudioEngine());
            world->chunkManager->setRenderDistance(gameSettings.renderDistance);
            w = &*world;

            if (hadSave && !skipPlayerRestore) {
                player.getCamera().setPosition(loaded.position);
                player.setYawPitch(loaded.yaw, loaded.pitch);
                player.getCamera().setWalkMode(loaded.walkMode);
                for (int i = 0; i < Player::HOTBAR_SIZE; i++) player.setHotbarSlot(i, loaded.hotbar[i]);
                player.setSelectedSlot(loaded.selectedSlot);
            } else {
                player.getCamera().setPosition(glm::vec3(15, 90, 15));
                player.setYawPitch(-90.0f, 0.0f);
                player.getCamera().setWalkMode(false);
            }

            // Persist so the new world shows up in listWorlds() immediately.
            // The player position has been restored to the saved (x, y, z,
            // yaw, pitch) but chunks are still streaming — the Loading state
            // pumps chunks and renders a progress bar before Playing starts.
            worldSave->saveLevelData(seed, captureCurrentPlayer());
            WorldSave::syncToDisk();
        };

        // Benchmark runs without showing a menu — load the world up front.
        // Interactive runs defer world loading until the user picks / creates
        // one in the Singleplayer menu.
#ifndef __EMSCRIPTEN__
        if (benchmarkMode) {
            std::string initialFolder = pickInitialFolder();
            bool freshWorld = !std::filesystem::exists("saves/" + initialFolder + "/level.dat");
            std::optional<unsigned int> cliSeed;
            if (worldSeed != 0 && freshWorld) cliSeed = worldSeed;
            switchToWorld(initialFolder, cliSeed, freshWorld ? initialFolder : "");
        }
#endif

        EntityCubeRenderer entityCubes;
        PlayerRenderer playerRenderer;
        // GL state for entityCubes + particles is created lazily on first render.

#ifdef __EMSCRIPTEN__
        static
#endif
            NetSession netSession;
        player.setNetSession(&netSession);

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

        // Cloud shader + VAOs. The pattern texture itself is generated per-world
        // by regenerateCloudPattern (above) so cloud layout tracks the world seed.
#ifdef __EMSCRIPTEN__
        static
#endif
            Shader cloudShader("assets/Shaders/cloud_vert.shd", "assets/Shaders/cloud_frag.shd");
        {
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
                float phi = u01(starRng) * 6.2831853f;             // [0, 2pi]
                float x = std::sin(theta) * std::cos(phi) * STAR_DIST;
                float y = std::cos(theta) * STAR_DIST; // always >= 0
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

                // Stress-water injection: right at the warmup→measure boundary,
                // place a grid of water sources around the player. The sim
                // will spend the measured phase processing flood → decay,
                // which exercises WaterSimulator::tick + chunk mesh rebuilds
                // (the actual hot paths the default benchmark scene misses).
                if (stressWater && frame == WARMUP_FRAMES) {
                    glm::vec3 pp = player.getPosition();
                    int ox = (int)std::floor(pp.x);
                    int oy = (int)std::floor(pp.y) + 6;
                    int oz = (int)std::floor(pp.z);
                    int placed = 0;
                    for (int dx = -8; dx <= 8; dx += 2) {
                        for (int dz = -8; dz <= 8; dz += 2) {
                            int wx = ox + dx, wz = oz + dz;
                            if (oy < 0 || oy >= CHUNK_HEIGHT) continue;
                            w->setBlock(wx, oy, wz, WATER, 0);
                            w->waterSimulator->activate(wx, oy, wz);
                            placed++;
                        }
                    }
                    std::cout << "[stress-water] placed " << placed << " sources at y=" << oy << std::endl;
                }

                if (frame < WARMUP_FRAMES) {
                    // Phase 1 (warmup): spin 360° in place so surrounding chunks load
                    float angle = glm::radians((float)frame / WARMUP_FRAMES * 360.0f);
                    player.getCamera().changeDirection(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
                } else if (stressWater) {
                    // Stay put looking down at the flood
                    player.getCamera().changeDirection(glm::vec3(0.3f, -0.6f, 0.0f));
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
                // Benchmark runs at a fixed step so frame-rate measurements
                // aren't perturbed by real dt; entities don't exist here.
                w->update(player.getPosition(), 1.0f / 60.0f, glfwGetTime());
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
                FrameMark;
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
        if (w) w->chunkManager->setRenderDistance(gameSettings.renderDistance);

        // Extract loop body into a lambda for Emscripten compatibility
        auto mainLoopBody = [&]() {
            GLFWwindow* window = g_window;
            Shader& shaderProgram = *g_shader;
            UIRenderer& uiRenderer = *g_uiRenderer;
            Menu& menu = *g_menu;

            // Menu / loading / pause states call glfwWaitEventsTimeout below
            // instead of glfwPollEvents, so the event loop idles up to a
            // 60 FPS budget rather than spinning the CPU at 1000+ fps. Input
            // events (mouse, keys) still wake the loop immediately.
            auto poll = [&]() {
                if (currentState != GameState::Playing) glfwWaitEventsTimeout(menuFrameBudgetSeconds());
                else glfwPollEvents();
            };

            // --- Menu states ---
            // No chunk pumping in menu states — we don't know which world
            // the user will pick, so any pre-loaded chunks would usually be
            // thrown away. switchToWorld() blocks until spawn chunks are
            // generated before returning Playing.
            if (currentState == GameState::MainMenu) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                GameState next = menu.drawMainMenu(uiRenderer, windowWidth, windowHeight, window);
                if (next == GameState::WorldList) menu.refreshWorldList();
                if (next == GameState::Settings) applySettings();
                currentState = next;
                glfwSwapBuffers(window);
                poll();
                return;
            }

            if (currentState == GameState::WorldList) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                WorldListResult res;
                GameState next = menu.drawWorldList(uiRenderer, windowWidth, windowHeight, window, res);
                auto currentFolder = [&]() -> std::string {
                    if (!worldSave) return "";
                    auto base = worldSave->getBasePath();
                    auto slash = base.find_last_of('/');
                    return slash == std::string::npos ? base : base.substr(slash + 1);
                };
                switch (res.action) {
                    case WorldListResult::PlaySelected:
                        if (res.folder != currentFolder()) switchToWorld(res.folder, std::nullopt, "");
                        if (world) {
                            glm::vec3 p = player.getPosition();
                            g_loadingSpawnCx = (int)std::floor(p.x / (float)CHUNK_SIZE);
                            g_loadingSpawnCz = (int)std::floor(p.z / (float)CHUNK_SIZE);
                            g_loadingStartTime = glfwGetTime();
                            g_loadingWorldName = worldSave ? worldSave->getDisplayName() : res.folder;
                            next = GameState::Loading;
                        }
                        break;
                    case WorldListResult::DeleteConfirmed: {
                        bool isCurrent = (res.folder == currentFolder());
                        if (isCurrent) {
                            world.reset();
                            worldSave.reset();
                            w = nullptr;
                        }
                        deleteWorld(res.folder);
                        WorldSave::syncToDisk();
                        menu.refreshWorldList();
                        if (isCurrent) {
                            // Re-load whichever world is now most-recent (or create a fresh one).
                            auto all = listWorlds();
                            std::string next = all.empty() ? uniqueFolderName("World") : all[0].folder;
                            switchToWorld(next, std::nullopt, "");
                        }
                        break;
                    }
                    case WorldListResult::RenameConfirmed: {
                        std::string newFolder;
                        bool isCurrent = (res.renameOld == currentFolder());
                        if (isCurrent) {
                            saveCurrentWorld();
                            world.reset();
                            worldSave.reset();
                            w = nullptr;
                        }
                        renameWorld(res.renameOld, res.renameNew, newFolder);
                        WorldSave::syncToDisk();
                        menu.refreshWorldList();
                        if (isCurrent) switchToWorld(newFolder, std::nullopt, "");
                        break;
                    }
                    case WorldListResult::CreateRequested:
                    case WorldListResult::BackToTitle:
                    case WorldListResult::None:
                    default:
                        break;
                }
                if (next == GameState::Playing) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                    menu.startMusic();
                }
                currentState = next;
                glfwSwapBuffers(window);
                poll();
                return;
            }

            if (currentState == GameState::CreateWorld) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                CreateWorldResult res;
                GameState next = menu.drawCreateWorld(uiRenderer, windowWidth, windowHeight, window, res);
                if (res.create) {
                    std::string displayName = sanitizeWorldName(res.name);
                    std::string folder = uniqueFolderName(displayName);
                    unsigned int seed = resolveSeed(res.seed);
                    switchToWorld(folder, std::optional<unsigned int>(seed), displayName);
                    glm::vec3 p = player.getPosition();
                    g_loadingSpawnCx = (int)std::floor(p.x / (float)CHUNK_SIZE);
                    g_loadingSpawnCz = (int)std::floor(p.z / (float)CHUNK_SIZE);
                    g_loadingStartTime = glfwGetTime();
                    g_loadingWorldName = displayName;
                    next = GameState::Loading;
                }
                if (next == GameState::WorldList) {
                    menu.refreshWorldList();
                }
                currentState = next;
                glfwSwapBuffers(window);
                poll();
                return;
            }

            if (currentState == GameState::Loading) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                // Pump chunk streaming and count how many of the 3x3 spawn
                // chunks are ready. Progress drives the bar on screen.
                int ready = 0;
                if (w) {
                    w->chunkManager->update(player.getPosition());
                    for (int dx = -1; dx <= 1; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                            if (w->chunkManager->getChunk(g_loadingSpawnCx + dx, g_loadingSpawnCz + dz)) ready++;
                }
                float progress = ready / 9.0f;
                // 10-second safety timeout — if chunks still haven't loaded,
                // enter Playing anyway so the user isn't stuck on this screen.
                bool timedOut = (glfwGetTime() - g_loadingStartTime) > 10.0;
                menu.drawLoadingScreen(uiRenderer, windowWidth, windowHeight, progress, g_loadingWorldName);
                if (ready >= 9 || timedOut || !w) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                    menu.startMusic();
                    currentState = GameState::Playing;
                }
                glfwSwapBuffers(window);
                poll();
                return;
            }

            if (currentState == GameState::Multiplayer) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                netSession.poll(glfwGetTime());
                GameState next = menu.drawMultiplayer(uiRenderer, windowWidth, windowHeight, window, netSession);
                if (next == GameState::Playing) {
                    // Both peers play in a shared-seed "multiplayer" world.
                    // MVP shortcut: chunks are generated locally on each side
                    // from the same seed, not synced over the data channel.
                    if (!w) switchToWorld("multiplayer", std::optional<unsigned int>(1234567u), "Multiplayer");
                    if (w) netSession.bindWorld(w);
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                    menu.startMusic();
                }
                currentState = next;
                glfwSwapBuffers(window);
                poll();
                return;
            }

            if (currentState == GameState::Settings) {
                glClearColor(0.2f, 0.15f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                GameState next = menu.drawSettings(uiRenderer, windowWidth, windowHeight, window, gameSettings);
                if (next != GameState::Settings) {
                    gameSettings.save(SETTINGS_PATH);
                    syncToPersistentStorage();
                    applySettings();
                    if (next == GameState::Playing) {
                        glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                        player.resetMouseState();
                        player.consumeMouseButtons();
                    }
                }
                currentState = next;
                glfwSwapBuffers(window);
                poll();
                return;
            }

            // --- Game rendering (used by both Playing and Paused) ---
            float speed = 0.025;
            float timeValue = 0.0f;
            if (doDaylightCycle) {
                // syncedTime() == glfwGetTime() when offline or hosting;
                // when a client is connected, it returns the host's clock
                // so the day/night cycle matches across peers.
                timeValue = (float)(netSession.syncedTime(glfwGetTime()) * speed);
            }
            float radius = 1000.0f;

            glm::vec3 skyColor = getSkyColor(timeValue);
            constexpr float WATER_SURFACE = CHUNK_HEIGHT / 2.0f;
            constexpr float WATER_DEPTH_RANGE = 15.0f;
            // Day-lit underwater colors. Scaled by the sun-height factor
            // at render time so the water darkens at night instead of
            // staying bright blue.
            const glm::vec3 SHALLOW_BLUE_DAY(0.15f, 0.4f, 0.7f);
            const glm::vec3 DEEP_BLUE_DAY(0.02f, 0.05f, 0.15f);
            // clamp(sin*2, 0.08, 1): 1.0 at noon → 0.08 floor at deep
            // night so the tint is still faintly blue, not pitch black.
            float daylight = std::clamp(std::sin(timeValue) * 2.0f, 0.08f, 1.0f);
            glm::vec3 SHALLOW_BLUE = SHALLOW_BLUE_DAY * daylight;
            glm::vec3 DEEP_BLUE = DEEP_BLUE_DAY * daylight;
            bool underwater = player.getCamera().areEyesInWater();
            float waterDepthFactor = 0.0f;
            if (underwater) {
                float depth = std::max(0.0f, WATER_SURFACE - player.getPosition().y);
                waterDepthFactor = std::min(depth / WATER_DEPTH_RANGE, 1.0f);
            }
            // Drip a trickle of motes into the volume around the camera so
            // the parallax reads as water rushing past when the player moves.
            // Rate is deliberately low — target ~30 motes in view at steady
            // state, not a sandstorm.
            static double lastDriftSpawn = 0.0;
            if (underwater && w && w->particles) {
                double now = glfwGetTime();
                if (now - lastDriftSpawn > 0.15) {
                    lastDriftSpawn = now;
                    // Brightness tracks time of day and dims with depth, so
                    // motes turn nearly invisible at night / deep under.
                    float driftLight = daylight * (1.0f - 0.6f * waterDepthFactor);
                    w->particles->spawnUnderwaterDrift(player.getPosition(), player.getFront(), 1, driftLight);
                }
            }
            glm::vec3 clearColor = underwater ? glm::mix(SHALLOW_BLUE, DEEP_BLUE, waterDepthFactor) : skyColor;
            glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
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

            // Fog: blend to sky color, or blue underwater fog
            float fogEnd = (float)(gameSettings.renderDistance * CHUNK_SIZE);
            float fogStart = fogEnd * 0.6f;
            glm::vec3 fogColor = skyColor;
            if (underwater) {
                fogEnd = 80.0f - 64.0f * waterDepthFactor;
                fogStart = fogEnd * 0.3f;
                fogColor = glm::mix(SHALLOW_BLUE, DEEP_BLUE, waterDepthFactor);
            }
            shaderProgram.setVec3("cameraPos", cameraPos);
            shaderProgram.setFloat("fogStart", fogStart);
            shaderProgram.setFloat("fogEnd", fogEnd);
            shaderProgram.setVec3("fogColor", fogColor);
            float gameTime = (float)glfwGetTime();
            shaderProgram.setFloat("time", gameTime);
            shaderProgram.setInt("fancyLeaves", g_fancyLeaves ? 1 : 0);
            shaderProgram.setVec2("leafSway",
                                  glm::vec2(std::sin(gameTime * 2.0f) * 0.06f, std::cos(gameTime * 1.5f) * 0.04f));

            // Targeting handled by player.update()

            TextureArray::bind();
            if (currentState == GameState::Playing) {
                w->update(player.getPosition(), dt, glfwGetTime());
                double netNow = glfwGetTime();
                netSession.poll(netNow);
                netSession.tickBroadcast(player, netNow);
            }
            player.getCamera().setShake(w ? w->cameraShake : 0.0f, glfwGetTime());
            glm::mat4 viewProjection = projection * player.getViewMatrix();

            // --- Billboard rendering (separate shader, no terrain features) ---
            if (!underwater) {
                billboardShader.use();
                billboardShader.setMat4("projection", projection);
                billboardShader.setMat4("view", player.getViewMatrix());
                billboardShader.setVec3("tintColor", glm::vec3(1.0f));
                TextureArray::bind();

                // Stars
                if (sunH < 0.1f) {
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
                    glm::vec3 upRef = (glm::abs(sunDir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

                    // Bloom halo: two additive radial-gradient quads of increasing
                    // size give a soft glow around the sun. Outer = wider/dimmer.
                    auto drawQuad = [&](float size, const glm::vec3& tint) {
                        glm::vec3 r = glm::normalize(glm::cross(sunDir, upRef)) * size;
                        glm::vec3 u = glm::normalize(glm::cross(r, sunDir)) * size;
                        float layer = (float)TextureArray::SUN_LAYER;
                        float verts[40] = {
                            sunCenter.x - r.x - u.x,
                            sunCenter.y - r.y - u.y,
                            sunCenter.z - r.z - u.z,
                            0,
                            0,
                            0,
                            0,
                            1,
                            layer,
                            1,
                            sunCenter.x - r.x + u.x,
                            sunCenter.y - r.y + u.y,
                            sunCenter.z - r.z + u.z,
                            0,
                            1,
                            0,
                            0,
                            1,
                            layer,
                            1,
                            sunCenter.x + r.x + u.x,
                            sunCenter.y + r.y + u.y,
                            sunCenter.z + r.z + u.z,
                            1,
                            1,
                            0,
                            0,
                            1,
                            layer,
                            1,
                            sunCenter.x + r.x - u.x,
                            sunCenter.y + r.y - u.y,
                            sunCenter.z + r.z - u.z,
                            1,
                            0,
                            0,
                            0,
                            1,
                            layer,
                            1,
                        };
                        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                        billboardShader.setVec3("tintColor", tint);
                        glBindVertexArray(sunVAO);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                    };

                    billboardShader.setMat4("model", glm::mat4(1.0f));
                    glDepthMask(GL_FALSE);
                    glDisable(GL_CULL_FACE);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive for glow
                    billboardShader.setFloat("glowMode", 1.0f);
                    float sunH = std::max(sunDir.y, 0.0f);
                    // Warm tint near horizon (orange), white at zenith
                    glm::vec3 haloTint = glm::mix(glm::vec3(1.2f, 0.6f, 0.3f), glm::vec3(1.0f, 0.95f, 0.85f), sunH);
                    drawQuad(SUN_SIZE * 4.5f, haloTint * 0.45f);
                    drawQuad(SUN_SIZE * 2.2f, haloTint * 0.8f);
                    billboardShader.setFloat("glowMode", 0.0f);

                    // Core sun disc (opaque texture, normal alpha blending)
                    drawQuad(SUN_SIZE, glm::vec3(1.0f));
                    glBindVertexArray(0);

                    glDepthMask(GL_TRUE);
                    glEnable(GL_CULL_FACE);
                    glDisable(GL_BLEND);
                }

                // Render moon billboard (opposite side of sun)
                if (lightPos.y < 200.0f) {                                    // moon rises before sun fully sets
                    glm::vec3 moonDir = glm::normalize(cameraPos - lightPos); // opposite of sun
                    glm::vec3 moonCenter = cameraPos + moonDir * SUN_DISTANCE;
                    constexpr float MOON_SIZE = 45.0f;
                    glm::vec3 upRef = (glm::abs(moonDir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

                    auto drawMoonQuad = [&](float size, const glm::vec3& tint, int layer) {
                        glm::vec3 r = glm::normalize(glm::cross(moonDir, upRef)) * size;
                        glm::vec3 u = glm::normalize(glm::cross(r, moonDir)) * size;
                        float l = (float)layer;
                        float verts[40] = {
                            moonCenter.x - r.x - u.x,
                            moonCenter.y - r.y - u.y,
                            moonCenter.z - r.z - u.z,
                            0,
                            0,
                            0,
                            0,
                            1,
                            l,
                            1,
                            moonCenter.x - r.x + u.x,
                            moonCenter.y - r.y + u.y,
                            moonCenter.z - r.z + u.z,
                            0,
                            1,
                            0,
                            0,
                            1,
                            l,
                            1,
                            moonCenter.x + r.x + u.x,
                            moonCenter.y + r.y + u.y,
                            moonCenter.z + r.z + u.z,
                            1,
                            1,
                            0,
                            0,
                            1,
                            l,
                            1,
                            moonCenter.x + r.x - u.x,
                            moonCenter.y + r.y - u.y,
                            moonCenter.z + r.z - u.z,
                            1,
                            0,
                            0,
                            0,
                            1,
                            l,
                            1,
                        };
                        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                        billboardShader.setVec3("tintColor", tint);
                        glBindVertexArray(sunVAO);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                    };

                    billboardShader.setMat4("model", glm::mat4(1.0f));
                    glDepthMask(GL_FALSE);
                    glDisable(GL_CULL_FACE);
                    glEnable(GL_BLEND);

                    // Bloom halo: dimmer and cooler than the sun.
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    billboardShader.setFloat("glowMode", 1.0f);
                    // Cool blue-white moonlight
                    glm::vec3 moonHalo(0.6f, 0.7f, 0.9f);
                    drawMoonQuad(MOON_SIZE * 3.5f, moonHalo * 0.2f, TextureArray::MOON_LAYER);
                    drawMoonQuad(MOON_SIZE * 1.8f, moonHalo * 0.35f, TextureArray::MOON_LAYER);
                    billboardShader.setFloat("glowMode", 0.0f);

                    // Core moon disc
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    drawMoonQuad(MOON_SIZE, glm::vec3(1.0f), TextureArray::MOON_LAYER);
                    glBindVertexArray(0);

                    glDepthMask(GL_TRUE);
                    glEnable(GL_CULL_FACE);
                    glDisable(GL_BLEND);
                }

            } // end !underwater sky rendering

            // Restore world shader after billboards
            shaderProgram.use();
            shaderProgram.setFloat("entityTint", 1.0f);
            shaderProgram.setVec3("entityColor", glm::vec3(1.0f));
            TextureArray::bind();
            chunksRendered = w->render(shaderProgram, viewProjection, player.getPosition());

            // Primed TNT entities — drawn after opaque chunks so they
            // correctly sort against the terrain. Chunk shader is already
            // bound with all its uniforms set.
            if (w->entityManager) {
                w->entityManager->render(shaderProgram, viewProjection, entityCubes, glfwGetTime());
                // Reset model matrix since entity draws mutated it.
                shaderProgram.setMat4("model", glm::mat4(1.0f));
                shaderProgram.setFloat("entityTint", 1.0f);
            }

            // Remote players (multiplayer MVP) — rendered as a Steve-shaped
            // humanoid built from 6 cube parts, tinted via entityColor.
            // Pose is interpolated between the last two received samples
            // so motion is smooth at the frame rate, not the 10 Hz wire rate.
            if (currentState == GameState::Playing && netSession.connected()) {
                double renderNow = glfwGetTime();
                double renderTime = renderNow - RENDER_DELAY;
                for (const auto& r : netSession.remotes()) {
                    RemotePlayer::Pose pose = r.sample(renderTime);
                    // The wire carries the peer's eye/camera position; Steve's
                    // feet are PLAYER_HEIGHT below that.
                    glm::vec3 feetPos = pose.pos - glm::vec3(0.0f, PLAYER_HEIGHT, 0.0f);
                    playerRenderer.draw(shaderProgram, feetPos, pose.yaw, pose.pitch);
                }
                shaderProgram.setMat4("model", glm::mat4(1.0f));
                shaderProgram.setFloat("entityTint", 1.0f);
                shaderProgram.setVec3("entityColor", glm::vec3(1.0f));
            }

            // Clouds: texture-based infinite tiling with 3D volume
            if (!underwater) {
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
                glm::mat4 cloudModel =
                    glm::translate(glm::mat4(1.0f), glm::vec3(cameraPos.x, CLOUD_Y_TOP, cameraPos.z));
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

            // Smoke particles (explosion plumes). Drawn after chunks/clouds
            // with alpha blending enabled so they fade softly into whatever
            // is behind them.
            if (w && w->particles && w->particles->aliveCount() > 0) {
                billboardShader.use();
                billboardShader.setMat4("projection", projection);
                billboardShader.setMat4("view", player.getViewMatrix());
                billboardShader.setVec3("tintColor", glm::vec3(1.0f));
                TextureArray::bind();
                glDepthMask(GL_FALSE);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                w->particles->render(billboardShader, cameraPos, player.getFront());
                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);
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
                    hlVerts[vi++] = 0;                                // normal
                    hlVerts[vi++] = (float)TextureArray::CLOUD_LAYER; // white
                    hlVerts[vi++] = 1;                                // ao
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

            // Block-placement "pop" — expanding white wireframe that fades out.
            // Shares highlightVAO with the targeted-block outline (re-uploaded each frame).
            {
                float p = player.getPlaceAnimProgress();
                if (p >= 0.0f) {
                    glm::ivec3 pp = player.getLastPlacedPos();
                    float x = (float)pp.x;
                    float y = (float)pp.y;
                    float z = (float)pp.z;
                    // Ease-out: snap outward fast, brightness fades faster than expansion
                    float ease = 1.0f - (1.0f - p) * (1.0f - p);
                    float e = 0.502f + 0.22f * ease;
                    float bright = 1.0f - p;

                    float popVerts[24 * 10];
                    int vi = 0;
                    auto addVert = [&](float vx, float vy, float vz) {
                        popVerts[vi++] = vx;
                        popVerts[vi++] = vy;
                        popVerts[vi++] = vz;
                        popVerts[vi++] = 0;
                        popVerts[vi++] = 0;
                        popVerts[vi++] = 0;
                        popVerts[vi++] = 0;
                        popVerts[vi++] = 0;
                        popVerts[vi++] = (float)TextureArray::CLOUD_LAYER;
                        popVerts[vi++] = 1;
                    };
                    float cx[8] = {x - e, x - e, x + e, x + e, x - e, x - e, x + e, x + e};
                    float cy[8] = {y - e, y + e, y + e, y - e, y - e, y + e, y + e, y - e};
                    float cz[8] = {z - e, z - e, z - e, z - e, z + e, z + e, z + e, z + e};
                    int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                    for (auto& edge : edges) {
                        addVert(cx[edge[0]], cy[edge[0]], cz[edge[0]]);
                        addVert(cx[edge[1]], cy[edge[1]], cz[edge[1]]);
                    }

                    glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(popVerts), popVerts);

                    billboardShader.use();
                    billboardShader.setMat4("projection", projection);
                    billboardShader.setMat4("view", player.getViewMatrix());
                    billboardShader.setMat4("model", glm::mat4(1.0f));
                    billboardShader.setVec3("tintColor", glm::vec3(bright));
                    glDisable(GL_CULL_FACE);
                    glLineWidth(3.0f);

                    glBindVertexArray(highlightVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);

                    glEnable(GL_CULL_FACE);
                    glLineWidth(2.0f);
                }
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

                // With perspective projection, the arm's screen-X slides
                // toward the center as the window gets wider (aspect
                // divides the X NDC). Scale the X translation by aspect so
                // the arm stays anchored to the bottom-right corner in
                // fullscreen widescreen.
                float aspect = (float)windowWidth / (float)windowHeight;

                block_type heldType = player.getSelectedBlockType();
                glDisable(GL_CULL_FACE);
                if (heldType == AIR) {
                    // Empty hand: show arm
                    armModel[3][0] *= aspect;
                    billboardShader.setMat4("model", armModel);
                    glBindVertexArray(armVAO);
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
                } else {
                    // Held block: smaller cube, tilted, in hand position
                    if (heldType != lastHeldBlock) buildHeldBlockMesh(heldType);
                    glm::mat4 blockModel = glm::mat4(1.0f);
                    blockModel = glm::translate(blockModel, glm::vec3(0.4f * aspect, -0.35f, -0.5f));
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
                constexpr float arm = 8.0f; // arm length
                constexpr float t = 2.0f;   // thickness
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
                uiRenderer.drawRect(barX, barY, totalW, SLOT_SIZE + BAR_PAD * 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
                const block_type* hotbar = player.getHotbar();
                int sel = player.getSelectedSlot();
                for (int i = 0; i < HSIZE; i++) {
                    float sx = barX + BAR_PAD + i * (SLOT_SIZE + SLOT_PAD);
                    float sy = barY + BAR_PAD;
                    // Slot background
                    glm::vec4 slotColor =
                        (i == sel) ? glm::vec4(1.0f, 1.0f, 1.0f, 0.4f) : glm::vec4(0.3f, 0.3f, 0.3f, 0.4f);
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

            // Inventory overlay (rendered over hotbar)
            if (currentState == GameState::Playing && inventory.isOpen()) {
                inventory.draw(uiRenderer, windowWidth, windowHeight, window, player);
            }

            // Pause menu overlay (rendered after the world)
            if (currentState == GameState::Paused) {
                GameState next = menu.drawPauseMenu(uiRenderer, windowWidth, windowHeight, window);
                if (next == GameState::Playing) {
                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_DISABLED);
                    player.resetMouseState();
                    player.consumeMouseButtons();
                    menu.startMusic();
                }
                if (next == GameState::Settings) {
                    gameSettings.save(SETTINGS_PATH);
                    syncToPersistentStorage();
                    applySettings();
                }
                if (next == GameState::MainMenu) {
                    if (saveCurrentWorld()) std::cout << "World saved" << std::endl;

                    glfwSetInputMode(window, CURSOR_MODE, GLFW_CURSOR_NORMAL);
                    menu.stopMusic();
                    menu.startMenuMusic();
                }
                currentState = next;
            }

            {
                ZoneScopedN("glfwSwapBuffers");
                glfwSwapBuffers(window);
            }
            {
                ZoneScopedN("glfwPollEvents");
                glfwPollEvents();
            }
            FrameMark;
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

        // Save world on exit
        if (saveCurrentWorld()) std::cout << "World saved" << std::endl;

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