#pragma once

#include <string>

enum class GameState { MainMenu, WorldList, CreateWorld, Settings, Loading, Playing, Paused, Multiplayer };

// Window resolution presets. Index 0 ("Auto") picks 80% of the primary
// monitor's current video mode at apply time. Queried via glfwGetVideoMode
// on native; ignored on the web (canvas size is driven by the page).
struct ResolutionPreset {
    int width;
    int height;
    const char* label;
};
inline constexpr ResolutionPreset RESOLUTION_PRESETS[] = {
    {0, 0, "Auto"},      {1280, 720, "1280 x 720"},  {1600, 900, "1600 x 900"},
    {1920, 1080, "1920 x 1080"}, {2560, 1440, "2560 x 1440"}, {3840, 2160, "3840 x 2160"},
};
inline constexpr int NUM_RESOLUTION_PRESETS =
    sizeof(RESOLUTION_PRESETS) / sizeof(RESOLUTION_PRESETS[0]);

struct GameSettings {
    int renderDistance = 16;
    float fov = 70.0f;
    bool vsync = false;
    float mouseSensitivity = 1.0f;
    bool greedyMeshing = false;
    bool fancyLeaves = true;
    float musicVolume = 1.0f;
    // Default to 1280 x 720 (index 1). Users on a small laptop panel
    // would be overwhelmed by Auto (full monitor) on first launch; they
    // can opt in via the Settings cycler.
    int resolutionIndex = 1;

    void load(const std::string& path);
    void save(const std::string& path) const;
};
