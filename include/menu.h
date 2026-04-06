#pragma once

#include "game_state.h"
#include "ui_renderer.h"
#include <GLFW/glfw3.h>
#include <miniaudio.h>
#include <string>

class Menu {
  public:
    void init();
    void destroy();

    // Each draw method returns the new state after processing input
    GameState drawMainMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window);
    GameState drawSettings(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, GameSettings& settings);
    GameState drawPauseMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window);

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
    double mouseX = 0, mouseY = 0;

    // ESC edge detection
    bool escWasPressed = false;

    // Active slider drag (-1 = none)
    int activeSlider = -1;

    void updateMouse(GLFWwindow* window);
    bool mouseClicked() const;
    bool mouseInRect(float x, float y, float w, float h) const;

    void drawDirtBackground(UIRenderer& ui, int windowW, int windowH);
    bool drawButton(UIRenderer& ui, const std::string& label, float x, float y, float w, float h);
    bool drawSlider(UIRenderer& ui, const std::string& label, int sliderID, float x, float y, float w, float h,
                    float& value, float minVal, float maxVal, const std::string& suffix = "");
    bool drawToggle(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool& value);
    bool escPressed(GLFWwindow* window);
};
