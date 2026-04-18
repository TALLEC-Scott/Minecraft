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
        // Stream music from disk instead of pre-decoding — MA_SOUND_FLAG_DECODE
        // would eagerly decompress these multi-MB MP3s at startup (~17 MB
        // compressed → ~170 MB PCM), stalling the main menu for seconds.
        // Streaming decodes chunk-by-chunk during playback.
        constexpr ma_uint32 MUSIC_FLAGS = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC;
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/calm1.mp3", MUSIC_FLAGS, nullptr, nullptr,
                                    &musicSound) == MA_SUCCESS) {
            musicLoaded = true;
            ma_sound_set_looping(&musicSound, MA_TRUE);
            ma_sound_set_volume(&musicSound, 1.0f);
        }
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/menu2.mp3", MUSIC_FLAGS, nullptr, nullptr,
                                    &menuMusicSound) == MA_SUCCESS) {
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

bool Menu::drawButton(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool enabled) {
    bool hovered = enabled && mouseInRect(x, y, w, h);
    bool clicked = hovered && mouseClicked();
    if (clicked) {
        playClick();
        clickConsumed = true; // consumed until mouse released
    }

    // Button background
    glm::vec4 bgColor;
    if (!enabled)
        bgColor = glm::vec4(0.18f, 0.18f, 0.20f, 0.85f);
    else if (hovered)
        bgColor = glm::vec4(0.45f, 0.45f, 0.55f, 0.85f);
    else
        bgColor = glm::vec4(0.25f, 0.25f, 0.30f, 0.85f);

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
    glm::vec4 textColor = enabled ? glm::vec4(1.0f) : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    ui.drawTextShadow(label, tx, ty, scale, textColor);

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

    if (drawButton(ui, "Singleplayer", btnX, startY, btnW, btnH)) next = GameState::WorldList;

    // Multiplayer placeholder — disabled until server/client support ships.
    drawButton(ui, "Multiplayer", btnX, startY + gap, btnW, btnH, /*enabled=*/false);

    if (drawButton(ui, "Settings", btnX, startY + gap * 2, btnW, btnH)) {
        settingsReturnState = GameState::MainMenu;
        next = GameState::Settings;
    }

#ifndef __EMSCRIPTEN__
    // Browsers don't let a page programmatically close a tab the user opened,
    // so Quit is desktop-only — GLFW's close flag drives the native main loop.
    if (drawButton(ui, "Quit Game", btnX, startY + gap * 3, btnW, btnH)) glfwSetWindowShouldClose(window, true);
#endif

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
    ui.drawTextShadow("v0.7.10", 4.0f, (float)windowH - 12.0f, verScale, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));

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

    // Resolution cycler: click to advance through RESOLUTION_PRESETS.
    {
        float ry = startY + gap * 7;
        bool hovered = mouseInRect(ctrlX, ry, ctrlW, ctrlH);
        bool clicked = hovered && mouseClicked();
        if (clicked) {
            settings.resolutionIndex = (settings.resolutionIndex + 1) % NUM_RESOLUTION_PRESETS;
            playClick();
        }
        glm::vec4 bg(0.2f, 0.2f, 0.2f, 0.85f);
        if (hovered) bg += glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);
        ui.drawRect(ctrlX - 1, ry - 1, ctrlW + 2, ctrlH + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
        ui.drawRect(ctrlX, ry, ctrlW, ctrlH, bg);
        float scale = 2.0f;
        std::string label = std::string("Resolution: ") + RESOLUTION_PRESETS[settings.resolutionIndex].label;
        float tw = ui.textWidth(label, scale);
        float th = ui.textHeight(scale);
        ui.drawTextShadow(label, ctrlX + (ctrlW - tw) / 2.0f, ry + (ctrlH - th) / 2.0f, scale);
    }
    float doneOffset = 8.5f;

    // Done button
    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    if (drawButton(ui, "Done", btnX, startY + gap * doneOffset, btnW, btnH)) next = settingsReturnState;

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

// --- Text input infrastructure ---

void Menu::onCharInput(unsigned int codepoint) {
    TextInput* in = activeInput();
    if (!in || !in->active) return;
    // Only printable ASCII (font is 8x8 bitmap, no unicode support)
    if (codepoint < 0x20 || codepoint > 0x7E) return;
    if (in->buffer.size() >= in->maxLen) return;
    in->buffer.insert(in->buffer.begin() + in->cursor, static_cast<char>(codepoint));
    in->cursor++;
}

void Menu::onKeyInput(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    // Latches flipped to true for one draw cycle, read + cleared by draw methods.
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) enterPressedLatch = true;
    else if (key == GLFW_KEY_ESCAPE) escPressedLatch = true;
    else if (key == GLFW_KEY_TAB) tabPressedLatch = true;
    else pendingKeys.push_back({key});
}

TextInput* Menu::activeInput() {
    if (renameInput.active) return &renameInput;
    if (createNameInput.active) return &createNameInput;
    if (createSeedInput.active) return &createSeedInput;
    return nullptr;
}

void Menu::applyPendingKeys(TextInput& in) {
    for (auto& e : pendingKeys) {
        switch (e.key) {
            case GLFW_KEY_BACKSPACE:
                if (in.cursor > 0) {
                    in.buffer.erase(in.buffer.begin() + (in.cursor - 1));
                    in.cursor--;
                }
                break;
            case GLFW_KEY_DELETE:
                if (in.cursor < in.buffer.size()) in.buffer.erase(in.buffer.begin() + in.cursor);
                break;
            case GLFW_KEY_LEFT:
                if (in.cursor > 0) in.cursor--;
                break;
            case GLFW_KEY_RIGHT:
                if (in.cursor < in.buffer.size()) in.cursor++;
                break;
            case GLFW_KEY_HOME:
                in.cursor = 0;
                break;
            case GLFW_KEY_END:
                in.cursor = in.buffer.size();
                break;
        }
    }
    pendingKeys.clear();
}

bool Menu::drawTextField(UIRenderer& ui, TextInput& in, float x, float y, float w, float h,
                         const std::string& placeholder) {
    bool hovered = mouseInRect(x, y, w, h);
    if (hovered && mouseClicked()) {
        // Deactivate other inputs, activate this one
        renameInput.active = false;
        createNameInput.active = false;
        createSeedInput.active = false;
        in.active = true;
        in.cursor = in.buffer.size();
        playClick();
        clickConsumed = true;
    }

    if (in.active) applyPendingKeys(in);

    glm::vec4 border = in.active ? glm::vec4(0.9f, 0.9f, 0.4f, 1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 0.9f);
    ui.drawRect(x - 1, y - 1, w + 2, h + 2, border);
    ui.drawRect(x, y, w, h, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f));

    float scale = 2.0f;
    float padding = 8.0f;
    float textY = y + (h - ui.textHeight(scale)) / 2.0f;
    bool showPlaceholder = in.buffer.empty() && !in.active;
    std::string shown = showPlaceholder ? placeholder : in.buffer;
    glm::vec4 textColor = showPlaceholder ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : glm::vec4(1.0f);
    ui.drawText(shown, x + padding, textY, scale, textColor);

    // Caret (blinking)
    if (in.active) {
        bool visible = std::fmod(glfwGetTime(), 1.0) < 0.5;
        if (visible) {
            std::string prefix = in.buffer.substr(0, in.cursor);
            float caretX = x + padding + ui.textWidth(prefix, scale);
            float caretH = ui.textHeight(scale);
            ui.drawRect(caretX, textY, 2, caretH, glm::vec4(1.0f));
        }
    }

    // Enter fires only when this field is the active one
    return in.active && enterPressedLatch;
}

// --- World list screen ---

void Menu::refreshWorldList() {
    worlds = listWorlds();
    if (selectedWorld >= (int)worlds.size()) selectedWorld = -1;
    // Drop any stale latches set while in a non-text state (Playing etc).
    enterPressedLatch = false;
    escPressedLatch = false;
    tabPressedLatch = false;
    pendingKeys.clear();
}

GameState Menu::drawWorldList(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window,
                              WorldListResult& out) {
    updateMouse(window);
    out.action = WorldListResult::None;
    GameState next = GameState::WorldList;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float titleScale = 3.0f;
    std::string title = "Select World";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, windowH * 0.06f, titleScale);

    float listW = 560.0f, rowH = 44.0f;
    float listX = cx - listW / 2.0f;
    float listY = windowH * 0.18f;
    float listH = windowH * 0.58f;

    // List background
    ui.drawRect(listX - 2, listY - 2, listW + 4, listH + 4, glm::vec4(0, 0, 0, 0.9f));
    ui.drawRect(listX, listY, listW, listH, glm::vec4(0.08f, 0.08f, 0.10f, 0.85f));

    bool blockInteraction = (overlay != OverlayNone);

    if (worlds.empty()) {
        std::string msg = "No worlds yet. Click 'Create New World' to start.";
        float s = 1.8f;
        ui.drawText(msg, cx - ui.textWidth(msg, s) / 2.0f, listY + listH / 2.0f - ui.textHeight(s) / 2.0f, s,
                    glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
    } else {
        // Rows — scroll clamped so you can't overscroll past the end.
        float totalH = worlds.size() * rowH;
        float maxScroll = std::max(0.0f, totalH - listH);
        worldListScroll = std::clamp(worldListScroll, 0.0f, maxScroll);

        for (std::size_t i = 0; i < worlds.size(); ++i) {
            float ry = listY + i * rowH - worldListScroll;
            if (ry + rowH < listY || ry > listY + listH) continue;

            bool selected = ((int)i == selectedWorld);
            bool hovered = !blockInteraction && mouseInRect(listX, ry, listW, rowH) && mouseX >= listX &&
                           mouseY >= listY && mouseY <= listY + listH;
            glm::vec4 bg = selected ? glm::vec4(0.35f, 0.45f, 0.35f, 0.9f)
                                    : hovered ? glm::vec4(0.22f, 0.22f, 0.26f, 0.9f)
                                              : glm::vec4(0.14f, 0.14f, 0.17f, 0.9f);
            ui.drawRect(listX, ry, listW, rowH - 2, bg);

            float nameScale = 2.0f;
            float dateScale = 1.4f;
            ui.drawTextShadow(worlds[i].name, listX + 14.0f, ry + 6.0f, nameScale);
            std::string rel = relativeTime(worlds[i].lastPlayed);
            float relW = ui.textWidth(rel, dateScale);
            ui.drawTextShadow(rel, listX + listW - relW - 14.0f, ry + rowH - ui.textHeight(dateScale) - 8.0f,
                              dateScale, glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));

            if (hovered && mouseClicked()) {
                double now = glfwGetTime();
                bool doublePlay = ((int)i == lastClickedWorld) && (now - lastClickTime) < 0.4;
                selectedWorld = (int)i;
                lastClickedWorld = (int)i;
                lastClickTime = now;
                playClick();
                clickConsumed = true;
                if (doublePlay) {
                    out.action = WorldListResult::PlaySelected;
                    out.folder = worlds[i].folder;
                    next = GameState::Playing;
                }
            }
        }
    }

    // Footer buttons: two rows
    float btnGap = 12.0f;
    float btnH = 38.0f;
    float footerY = listY + listH + 18.0f;
    float halfW = (listW - btnGap) / 2.0f;
    float thirdW = (listW - 2 * btnGap) / 3.0f;

    bool hasSel = selectedWorld >= 0 && selectedWorld < (int)worlds.size();

    // Row 1
    if (!blockInteraction && drawButton(ui, "Play Selected", listX, footerY, halfW, btnH, hasSel)) {
        out.action = WorldListResult::PlaySelected;
        out.folder = worlds[selectedWorld].folder;
        next = GameState::Playing;
    }
    if (!blockInteraction && drawButton(ui, "Create New World", listX + halfW + btnGap, footerY, halfW, btnH)) {
        out.action = WorldListResult::CreateRequested;
        next = GameState::CreateWorld;
    }
    // Row 2
    float footerY2 = footerY + btnH + btnGap;
    if (!blockInteraction && drawButton(ui, "Rename", listX, footerY2, thirdW, btnH, hasSel)) {
        overlay = OverlayRename;
        renameInput.active = true;
        renameInput.setText(worlds[selectedWorld].name);
    }
    if (!blockInteraction &&
        drawButton(ui, "Delete", listX + thirdW + btnGap, footerY2, thirdW, btnH, hasSel)) {
        overlay = OverlayConfirmDelete;
    }
    if (!blockInteraction &&
        drawButton(ui, "Cancel", listX + 2 * (thirdW + btnGap), footerY2, thirdW, btnH)) {
        out.action = WorldListResult::BackToTitle;
        next = GameState::MainMenu;
    }

    // --- Overlays ---
    if (overlay == OverlayRename) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0, 0, 0, 0.65f));
        float ow = 520.0f, oh = 220.0f;
        float ox = cx - ow / 2.0f, oy = windowH * 0.32f;
        ui.drawRect(ox - 2, oy - 2, ow + 4, oh + 4, glm::vec4(0, 0, 0, 0.95f));
        ui.drawRect(ox, oy, ow, oh, glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
        std::string t = "Rename World";
        ui.drawTextShadow(t, cx - ui.textWidth(t, 2.2f) / 2.0f, oy + 16.0f, 2.2f);

        bool submitted = drawTextField(ui, renameInput, ox + 20.0f, oy + 72.0f, ow - 40.0f, 40.0f, "New name");
        float bw = (ow - 60.0f) / 2.0f;
        bool confirmClick = drawButton(ui, "Confirm", ox + 20.0f, oy + oh - 56.0f, bw, 40.0f);
        bool cancelClick = drawButton(ui, "Cancel", ox + 40.0f + bw, oy + oh - 56.0f, bw, 40.0f);

        if (cancelClick || escPressedLatch) {
            overlay = OverlayNone;
            renameInput.active = false;
            renameInput.buffer.clear();
        } else if ((confirmClick || submitted) && hasSel) {
            out.action = WorldListResult::RenameConfirmed;
            out.renameOld = worlds[selectedWorld].folder;
            out.renameNew = renameInput.buffer;
            overlay = OverlayNone;
            renameInput.active = false;
        }
    } else if (overlay == OverlayConfirmDelete) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0, 0, 0, 0.65f));
        float ow = 560.0f, oh = 200.0f;
        float ox = cx - ow / 2.0f, oy = windowH * 0.34f;
        ui.drawRect(ox - 2, oy - 2, ow + 4, oh + 4, glm::vec4(0, 0, 0, 0.95f));
        ui.drawRect(ox, oy, ow, oh, glm::vec4(0.18f, 0.10f, 0.10f, 1.0f));
        std::string t = "Delete World?";
        ui.drawTextShadow(t, cx - ui.textWidth(t, 2.2f) / 2.0f, oy + 16.0f, 2.2f);
        std::string m = hasSel ? ("'" + worlds[selectedWorld].name + "' will be permanently removed.")
                               : "";
        ui.drawText(m, cx - ui.textWidth(m, 1.4f) / 2.0f, oy + 70.0f, 1.4f,
                    glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        float bw = (ow - 60.0f) / 2.0f;
        bool yes = drawButton(ui, "Yes, delete", ox + 20.0f, oy + oh - 56.0f, bw, 40.0f);
        bool no = drawButton(ui, "Cancel", ox + 40.0f + bw, oy + oh - 56.0f, bw, 40.0f);
        if (no || escPressedLatch) {
            overlay = OverlayNone;
        } else if (yes && hasSel) {
            out.action = WorldListResult::DeleteConfirmed;
            out.folder = worlds[selectedWorld].folder;
            overlay = OverlayNone;
        }
    }

    // Consume one-shot latches
    enterPressedLatch = false;
    escPressedLatch = false;
    tabPressedLatch = false;

    ui.end();
    return next;
}

// --- Create world screen ---

GameState Menu::drawCreateWorld(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window,
                                CreateWorldResult& out) {
    updateMouse(window);
    out.create = false;
    GameState next = GameState::CreateWorld;

    // First-frame initialisation
    if (!createInitialized) {
        createNameInput.setText(uniqueFolderName(sanitizeWorldName("New World")));
        createNameInput.active = true;
        createSeedInput.setText("");
        createSeedInput.active = false;
        createShowAdvanced = false;
        createInitialized = true;
        enterPressedLatch = false;
        escPressedLatch = false;
        tabPressedLatch = false;
        pendingKeys.clear();
    }

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float titleScale = 3.0f;
    std::string title = "Create New World";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, windowH * 0.08f, titleScale);

    float panelW = 560.0f;
    float panelX = cx - panelW / 2.0f;
    float y = windowH * 0.22f;
    float labelScale = 1.8f;

    ui.drawText("World Name", panelX, y, labelScale);
    y += 28.0f;
    bool nameEnter = drawTextField(ui, createNameInput, panelX, y, panelW, 40.0f, "My World");
    y += 56.0f;

    std::string adv = createShowAdvanced ? "More World Options (hide)" : "More World Options";
    if (drawButton(ui, adv, panelX, y, panelW, 36.0f)) createShowAdvanced = !createShowAdvanced;
    y += 52.0f;

    bool seedEnter = false;
    if (createShowAdvanced) {
        ui.drawText("Seed (blank for random; numeric or text)", panelX, y, 1.4f,
                    glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));
        y += 22.0f;
        seedEnter = drawTextField(ui, createSeedInput, panelX, y, panelW, 40.0f,
                                  "blank = random");
        y += 56.0f;
    }

    // Footer
    float footerY = windowH - 80.0f;
    float bw = (panelW - 20.0f) / 2.0f;
    bool createClick = drawButton(ui, "Create", panelX, footerY, bw, 44.0f);
    bool cancelClick = drawButton(ui, "Cancel", panelX + bw + 20.0f, footerY, bw, 44.0f);

    // Tab cycles Name <-> Seed (only when advanced visible)
    if (tabPressedLatch && createShowAdvanced) {
        if (createNameInput.active) {
            createNameInput.active = false;
            createSeedInput.active = true;
            createSeedInput.cursor = createSeedInput.buffer.size();
        } else {
            createSeedInput.active = false;
            createNameInput.active = true;
            createNameInput.cursor = createNameInput.buffer.size();
        }
    }

    if (cancelClick || escPressedLatch) {
        createInitialized = false;
        createNameInput.active = false;
        createSeedInput.active = false;
        next = GameState::WorldList;
    } else if (createClick || nameEnter || seedEnter) {
        out.create = true;
        out.name = createNameInput.buffer;
        out.seed = createSeedInput.buffer;
        createInitialized = false;
        createNameInput.active = false;
        createSeedInput.active = false;
        next = GameState::Playing;
    }

    enterPressedLatch = false;
    escPressedLatch = false;
    tabPressedLatch = false;

    ui.end();
    return next;
}

// --- Loading screen ---

void Menu::drawLoadingScreen(UIRenderer& ui, int windowW, int windowH, float progress,
                             const std::string& worldName) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH / 2.0f;

    std::string title = "Loading world";
    float titleScale = 3.0f;
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy - 80.0f, titleScale);

    if (!worldName.empty()) {
        float s = 1.8f;
        ui.drawTextShadow(worldName, cx - ui.textWidth(worldName, s) / 2.0f, cy - 30.0f, s,
                          glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
    }

    // Progress bar — border, background, filled portion.
    float barW = 420.0f;
    float barH = 24.0f;
    float barX = cx - barW / 2.0f;
    float barY = cy + 10.0f;
    ui.drawRect(barX - 2, barY - 2, barW + 4, barH + 4, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    ui.drawRect(barX, barY, barW, barH, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f));
    ui.drawRect(barX, barY, barW * progress, barH, glm::vec4(0.35f, 0.65f, 0.35f, 0.95f));

    // Percentage label centered over the bar
    std::string pct = std::to_string((int)(progress * 100.0f + 0.5f)) + "%";
    float ps = 1.4f;
    ui.drawTextShadow(pct, cx - ui.textWidth(pct, ps) / 2.0f,
                      barY + (barH - ui.textHeight(ps)) / 2.0f, ps);

    ui.end();
}
