#pragma once

#include <string>

enum class GameState { MainMenu, Settings, Playing, Paused };

struct GameSettings {
    int renderDistance = 16;
    float fov = 45.0f;
    bool vsync = false;
    float mouseSensitivity = 1.0f;

    void load(const std::string& path);
    void save(const std::string& path) const;
};
