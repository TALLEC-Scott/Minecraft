#pragma once

#include <string>

enum class GameState { MainMenu, Settings, Playing, Paused };

struct GameSettings {
#ifdef __EMSCRIPTEN__
    int renderDistance = 8;
#else
    int renderDistance = 16;
#endif
    float fov = 70.0f;
    bool vsync = false;
    float mouseSensitivity = 1.0f;
    bool greedyMeshing = false;

    void load(const std::string& path);
    void save(const std::string& path) const;
};
