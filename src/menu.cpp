#include "menu.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

void Menu::init() {
    dirtTexture = UIRenderer::loadTexture("assets/Textures/dirt.png", true);

    // Initialize audio engine and preload click sound
    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &audioEngine) == MA_SUCCESS) {
        audioInitialized = true;
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/click.wav", MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                    &clickSound) == MA_SUCCESS) {
            clickLoaded = true;
        }
        // Load background music (decoded into memory for smooth playback)
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/calm1.mp3", MA_SOUND_FLAG_DECODE, nullptr,
                                    nullptr, &musicSound) == MA_SUCCESS) {
            musicLoaded = true;
            ma_sound_set_looping(&musicSound, MA_TRUE);
            ma_sound_set_volume(&musicSound, 1.0f);
        }
        // Load menu music
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/menu2.mp3", MA_SOUND_FLAG_DECODE, nullptr,
                                    nullptr, &menuMusicSound) == MA_SUCCESS) {
            menuMusicLoaded = true;
            ma_sound_set_looping(&menuMusicSound, MA_TRUE);
            ma_sound_set_volume(&menuMusicSound, 1.0f);
        }
    }
}

void Menu::destroy() {
    if (dirtTexture) glDeleteTextures(1, &dirtTexture);
    if (menuMusicLoaded) ma_sound_uninit(&menuMusicSound);
    if (musicLoaded) ma_sound_uninit(&musicSound);
    if (clickLoaded) ma_sound_uninit(&clickSound);
    if (audioInitialized) ma_engine_uninit(&audioEngine);
}

void Menu::startMusic() {
    if (musicLoaded && !musicPlaying) {
        stopMenuMusic();
        ma_sound_start(&musicSound);
        musicPlaying = true;
    }
}

void Menu::stopMusic() {
    if (musicLoaded && musicPlaying) {
        ma_sound_stop(&musicSound);
        musicPlaying = false;
    }
}

void Menu::startMenuMusic() {
    if (menuMusicLoaded && !menuMusicPlaying) {
        ma_sound_start(&menuMusicSound);
        menuMusicPlaying = true;
    }
}

void Menu::setMusicVolume(float vol) {
    if (musicLoaded) ma_sound_set_volume(&musicSound, vol);
    if (menuMusicLoaded) ma_sound_set_volume(&menuMusicSound, vol);
}

void Menu::stopMenuMusic() {
    if (menuMusicLoaded && menuMusicPlaying) {
        ma_sound_stop(&menuMusicSound);
        menuMusicPlaying = false;
    }
}

void Menu::playClick() {
    if (clickLoaded) {
        ma_sound_seek_to_pcm_frame(&clickSound, 0);
        ma_sound_start(&clickSound);
    }
}

void Menu::updateMouse(GLFWwindow* window) {
    glfwGetCursorPos(window, &mouseX, &mouseY);
    bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!down) clickConsumed = false; // reset when mouse released
    mouseWasPressed = mouseIsDown;
    mouseIsDown = down && !clickConsumed;
    if (!down) activeSlider = -1;
}

bool Menu::mouseClicked() const {
    return mouseIsDown && !mouseWasPressed;
}

bool Menu::mouseInRect(float x, float y, float w, float h) const {
    return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
}

void Menu::drawDirtBackground(UIRenderer& ui, int windowW, int windowH) {
    if (!dirtTexture) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.3f, 0.2f, 0.15f, 1.0f));
        return;
    }
    constexpr float TILE_SIZE = 64.0f;
    float tilesX = (float)windowW / TILE_SIZE;
    float tilesY = (float)windowH / TILE_SIZE;
    // Diagonal scroll offset
    float scroll = (float)glfwGetTime() * 0.15f;
    ui.drawTexturedRect(0, 0, (float)windowW, (float)windowH, dirtTexture, scroll, scroll, tilesX + scroll,
                        tilesY + scroll, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));
}

bool Menu::drawButton(UIRenderer& ui, const std::string& label, float x, float y, float w, float h) {
    bool hovered = mouseInRect(x, y, w, h);
    bool clicked = hovered && mouseClicked();
    if (clicked) {
        playClick();
        clickConsumed = true; // consumed until mouse released
    }

    // Button background
    glm::vec4 bgColor = hovered ? glm::vec4(0.45f, 0.45f, 0.55f, 0.85f) : glm::vec4(0.25f, 0.25f, 0.30f, 0.85f);

    // Border (slightly larger rect behind)
    ui.drawRect(x - 1, y - 1, w + 2, h + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    ui.drawRect(x, y, w, h, bgColor);

    // Highlight line at top
    ui.drawRect(x, y, w, 1, glm::vec4(0.6f, 0.6f, 0.7f, 0.5f));

    // Centered text
    float scale = 2.0f;
    float tw = ui.textWidth(label, scale);
    float th = ui.textHeight(scale);
    float tx = x + (w - tw) / 2.0f;
    float ty = y + (h - th) / 2.0f;
    ui.drawTextShadow(label, tx, ty, scale);

    return clicked;
}

bool Menu::drawSlider(UIRenderer& ui, const std::string& label, int sliderID, float x, float y, float w, float h,
                      float& value, float minVal, float maxVal, const std::string& suffix) {
    bool changed = false;

    // If this slider is active (being dragged), update value from mouse
    if (activeSlider == sliderID && mouseIsDown) {
        float t = ((float)mouseX - x) / w;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        changed = true;
    }

    // Start drag if clicked on this slider
    bool hovered = mouseInRect(x, y, w, h);
    if (hovered && mouseIsDown && activeSlider == -1) {
        activeSlider = sliderID;
        float t = ((float)mouseX - x) / w;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        changed = true;
    }

    // Background
    ui.drawRect(x - 1, y - 1, w + 2, h + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    ui.drawRect(x, y, w, h, glm::vec4(0.2f, 0.2f, 0.25f, 0.85f));

    // Fill bar
    float t = (value - minVal) / (maxVal - minVal);
    ui.drawRect(x, y, w * t, h, glm::vec4(0.35f, 0.55f, 0.35f, 0.85f));

    // Handle
    float handleX = x + w * t - 3;
    ui.drawRect(handleX, y, 6, h, glm::vec4(0.9f, 0.9f, 0.9f, 0.9f));

    // Label + value text
    float scale = 2.0f;
    std::stringstream ss;
    if (maxVal - minVal > 10 && minVal >= 0)
        ss << label << ": " << (int)value << suffix;
    else
        ss << label << ": " << std::fixed << std::setprecision(1) << value << suffix;
    std::string text = ss.str();
    float tw = ui.textWidth(text, scale);
    float th = ui.textHeight(scale);
    float tx = x + (w - tw) / 2.0f;
    float ty = y + (h - th) / 2.0f;
    ui.drawTextShadow(text, tx, ty, scale);

    return changed;
}

bool Menu::drawToggle(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool& value) {
    bool hovered = mouseInRect(x, y, w, h);
    bool clicked = hovered && mouseClicked();
    if (clicked) {
        value = !value;
        playClick();
    }

    glm::vec4 bgColor = value ? glm::vec4(0.35f, 0.55f, 0.35f, 0.85f) : glm::vec4(0.4f, 0.2f, 0.2f, 0.85f);
    if (hovered) bgColor += glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);

    ui.drawRect(x - 1, y - 1, w + 2, h + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    ui.drawRect(x, y, w, h, bgColor);

    float scale = 2.0f;
    std::string text = label + ": " + (value ? "ON" : "OFF");
    float tw = ui.textWidth(text, scale);
    float th = ui.textHeight(scale);
    ui.drawTextShadow(text, x + (w - tw) / 2.0f, y + (h - th) / 2.0f, scale);

    return clicked;
}

bool Menu::escPressed(GLFWwindow* window) {
#ifdef __EMSCRIPTEN__
    bool down = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
#else
    bool down = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
#endif
    bool pressed = down && !escWasPressed;
    escWasPressed = down;
    return pressed;
}

GameState Menu::drawMainMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window) {
    updateMouse(window);
    GameState next = GameState::MainMenu;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    // Title
    float titleScale = 4.0f;
    std::string title = "MINECRAFT";
    float titleW = ui.textWidth(title, titleScale);
    float titleY = cy * 0.18f;
    ui.drawTextShadow(title, cx - titleW / 2.0f, titleY, titleScale, glm::vec4(1.0f, 1.0f, 0.4f, 1.0f));

    // Subtitle
    float subScale = 1.5f;
    std::string subtitle = "C++ / OpenGL Edition";
    float subW = ui.textWidth(subtitle, subScale);
    ui.drawTextShadow(subtitle, cx - subW / 2.0f, titleY + titleScale * 8.0f + 8.0f, subScale,
                      glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

    // Buttons
    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    float startY = cy * 0.45f;
    float gap = 56.0f;

    if (drawButton(ui, "Singleplayer", btnX, startY, btnW, btnH)) next = GameState::Playing;

    if (drawButton(ui, "Settings", btnX, startY + gap, btnW, btnH)) {
        settingsReturnState = GameState::MainMenu;
        next = GameState::Settings;
    }

    if (drawButton(ui, "Quit Game", btnX, startY + gap * 2, btnW, btnH)) glfwSetWindowShouldClose(window, true);

    // Splash text — rotated, pulsing, bottom-right corner
    {
        float t = (float)glfwGetTime();
        float pulse = 2.0f + 0.2f * std::sin(t * 4.0f);
        float px = (float)windowW - 120.0f;
        float py = (float)windowH - 80.0f;
        ui.drawTextRotated("By Sc077y", px, py, pulse, -25.0f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    }

    // Version text
    float verScale = 1.0f;
    ui.drawTextShadow("v0.4.0", 4.0f, (float)windowH - 12.0f, verScale, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));

    ui.end();
    return next;
}

GameState Menu::drawSettings(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, GameSettings& settings) {
    updateMouse(window);
    GameState next = GameState::Settings;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    // Title
    float titleScale = 3.0f;
    std::string title = "Settings";
    float titleW = ui.textWidth(title, titleScale);
    ui.drawTextShadow(title, cx - titleW / 2.0f, cy * 0.08f, titleScale);

    // Settings controls
    float ctrlW = 400.0f, ctrlH = 40.0f;
    float ctrlX = cx - ctrlW / 2.0f;
    float startY = cy * 0.25f;
    float gap = 52.0f;

    float rd = (float)settings.renderDistance;
    drawSlider(ui, "Render Distance", 0, ctrlX, startY, ctrlW, ctrlH, rd, 4.0f, 32.0f, " chunks");
    settings.renderDistance = (int)rd;

    drawSlider(ui, "FOV", 1, ctrlX, startY + gap, ctrlW, ctrlH, settings.fov, 30.0f, 110.0f);

    drawSlider(ui, "Sensitivity", 2, ctrlX, startY + gap * 2, ctrlW, ctrlH, settings.mouseSensitivity, 0.1f, 3.0f);

    drawToggle(ui, "VSync", ctrlX, startY + gap * 3, ctrlW, ctrlH, settings.vsync);

    drawToggle(ui, "Greedy Meshing", ctrlX, startY + gap * 4, ctrlW, ctrlH, settings.greedyMeshing);

    drawToggle(ui, "Fancy Leaves", ctrlX, startY + gap * 5, ctrlW, ctrlH, settings.fancyLeaves);

    drawSlider(ui, "Music", 3, ctrlX, startY + gap * 6, ctrlW, ctrlH, settings.musicVolume, 0.0f, 1.0f);

    // Done button
    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    if (drawButton(ui, "Done", btnX, startY + gap * 7.5f, btnW, btnH)) next = settingsReturnState;

    if (escPressed(window)) next = settingsReturnState;

    ui.end();
    return next;
}

GameState Menu::drawPauseMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window) {
    updateMouse(window);
    GameState next = GameState::Paused;

    ui.begin(windowW, windowH);

    // Dark overlay over the world
    ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    // Title
    float titleScale = 3.0f;
    std::string title = "Game Menu";
    float titleW = ui.textWidth(title, titleScale);
    ui.drawTextShadow(title, cx - titleW / 2.0f, cy * 0.2f, titleScale);

    // Buttons
    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    float startY = cy * 0.38f;
    float gap = 56.0f;

    if (drawButton(ui, "Back to Game", btnX, startY, btnW, btnH)) next = GameState::Playing;

    if (drawButton(ui, "Settings", btnX, startY + gap, btnW, btnH)) {
        settingsReturnState = GameState::Paused;
        next = GameState::Settings;
    }

    if (drawButton(ui, "Quit to Title", btnX, startY + gap * 2, btnW, btnH)) next = GameState::MainMenu;

    if (escPressed(window)) next = GameState::Playing;

    ui.end();
    return next;
}
