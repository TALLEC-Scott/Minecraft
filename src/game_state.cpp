#include "game_state.h"
#include <algorithm>
#include <fstream>
#include <sstream>

void GameSettings::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "renderDistance")
            renderDistance = std::clamp(std::stoi(val), 4, 32);
        else if (key == "fov")
            fov = std::clamp(std::stof(val), 30.0f, 110.0f);
        else if (key == "vsync")
            vsync = (val == "1");
        else if (key == "mouseSensitivity")
            mouseSensitivity = std::clamp(std::stof(val), 0.1f, 3.0f);
        else if (key == "greedyMeshing")
            greedyMeshing = (val == "1");
        else if (key == "musicVolume")
            musicVolume = std::clamp(std::stof(val), 0.0f, 1.0f);
    }
}

void GameSettings::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return;
    file << "renderDistance=" << renderDistance << "\n";
    file << "fov=" << fov << "\n";
    file << "vsync=" << (vsync ? 1 : 0) << "\n";
    file << "mouseSensitivity=" << mouseSensitivity << "\n";
    file << "greedyMeshing=" << (greedyMeshing ? 1 : 0) << "\n";
    file << "musicVolume=" << musicVolume << "\n";
}
