#pragma once

#include "game_state.h"
#include "ui_renderer.h"
#include "widgets.h"
#include "world_directory.h"
#include <GLFW/glfw3.h>
#include <miniaudio.h>
#include <string>
#include <vector>

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

    // All widget drawing / mouse / keyboard routing lives in Widgets. See
    // include/widgets.h. Menu composes screens by calling widgets.button etc.
    Widgets widgets;

    void drawDirtBackground(UIRenderer& ui, int windowW, int windowH);

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
};
