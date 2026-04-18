#pragma once

#include "game_state.h"
#include "ui_renderer.h"
#include "world_directory.h"
#include <GLFW/glfw3.h>
#include <miniaudio.h>
#include <string>
#include <vector>

struct TextInput {
    std::string buffer;
    std::size_t cursor = 0;
    std::size_t maxLen = 48;
    bool active = false;
    void setText(const std::string& s) {
        buffer = s.size() > maxLen ? s.substr(0, maxLen) : s;
        cursor = buffer.size();
    }
};

struct CreateWorldResult {
    bool create = false;
    std::string name;
    std::string seed;
};

struct WorldListResult {
    enum Action { None, PlaySelected, CreateRequested, DeleteConfirmed, RenameConfirmed, BackToTitle };
    Action action = None;
    std::string folder;       // for Play / Delete
    std::string renameOld;    // for Rename
    std::string renameNew;
};

class Menu {
  public:
    void init();
    void destroy();

    // Each draw method returns the new state after processing input
    GameState drawMainMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window);
    GameState drawSettings(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, GameSettings& settings);
    GameState drawPauseMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window);

    // World picker. Refreshes the cached world list on state entry; fills `out`
    // with the action taken (play / create / delete / rename / back).
    GameState drawWorldList(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, WorldListResult& out);
    // World creation screen — returns CreateWorld while the user is editing,
    // MainMenu when cancelling, or CreateWorld with `outCreate.create==true`
    // for the caller to act on.
    GameState drawCreateWorld(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, CreateWorldResult& out);

    // Loading screen with progress bar (progress in [0, 1]).
    void drawLoadingScreen(UIRenderer& ui, int windowW, int windowH, float progress,
                           const std::string& worldName);

    // Text-input callbacks (routed from main.cpp's char/key callbacks).
    // Only apply to whichever input is currently active.
    void onCharInput(unsigned int codepoint);
    void onKeyInput(int key, int action);
    // Refresh cached world list (call when transitioning into WorldList state).
    void refreshWorldList();

    // Track where Settings was opened from
    GameState settingsReturnState = GameState::MainMenu;

    ma_engine* getAudioEngine() { return audioInitialized ? &audioEngine : nullptr; }
    void startMusic();
    void stopMusic();
    void startMenuMusic();
    void stopMenuMusic();
    void setMusicVolume(float vol);

  private:
    GLuint dirtTexture = 0;

    // Audio
    ma_engine audioEngine;
    ma_sound clickSound;
    ma_sound musicSound;
    ma_sound menuMusicSound;
    bool audioInitialized = false;
    bool clickLoaded = false;
    bool musicLoaded = false;
    bool musicPlaying = false;
    bool menuMusicLoaded = false;
    bool menuMusicPlaying = false;
    void playClick();

    // Mouse state for click detection
    bool mouseWasPressed = false;
    bool mouseIsDown = false;
    bool clickConsumed = false;
    double mouseX = 0, mouseY = 0;

    // ESC edge detection
    bool escWasPressed = false;

    // Active slider drag (-1 = none)
    int activeSlider = -1;

    void updateMouse(GLFWwindow* window);
    bool mouseClicked() const;
    bool mouseInRect(float x, float y, float w, float h) const;

    void drawDirtBackground(UIRenderer& ui, int windowW, int windowH);
    bool drawButton(UIRenderer& ui, const std::string& label, float x, float y, float w, float h,
                    bool enabled = true);
    bool drawSlider(UIRenderer& ui, const std::string& label, int sliderID, float x, float y, float w, float h,
                    float& value, float minVal, float maxVal, const std::string& suffix = "");
    bool drawToggle(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool& value);
    bool escPressed(GLFWwindow* window);

    // Draws a text input; click inside activates it. Returns true if Enter was pressed this frame.
    bool drawTextField(UIRenderer& ui, TextInput& in, float x, float y, float w, float h,
                       const std::string& placeholder);

    // WorldList state
    std::vector<WorldEntry> worlds;
    int selectedWorld = -1;
    float worldListScroll = 0.0f;
    enum Overlay { OverlayNone, OverlayRename, OverlayConfirmDelete };
    Overlay overlay = OverlayNone;
    TextInput renameInput;
    double lastClickTime = 0.0;
    int lastClickedWorld = -1;

    // CreateWorld state
    TextInput createNameInput;
    TextInput createSeedInput;
    bool createShowAdvanced = false;
    bool createInitialized = false;

    // Pending key events for active text input (populated via onKeyInput).
    struct PendingKey { int key; };
    std::vector<PendingKey> pendingKeys;
    bool enterPressedLatch = false;
    bool escPressedLatch = false;
    bool tabPressedLatch = false;

    TextInput* activeInput();
    void applyPendingKeys(TextInput& in);
};
