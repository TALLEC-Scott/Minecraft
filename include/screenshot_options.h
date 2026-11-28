#pragma once

// Desktop debug capture (main.cpp): stage a scene in a throwaway world and
// save frames as binary PPM, then exit. Used to check visuals on the GPU
// box (WSL2 renders slowly; run the Windows build there).
//
//   minecraft.exe --seed 42 --screenshot shots
//                 [--shot-at X,Y,Z,YAW,PITCH]   eye position (blocks), degrees
//                 [--shot-time day|night]       default day (noon / midnight)
//                 [--shot-frames N]             default 1, 50 ms apart
//                 [--shot-item ID]              item id in hand (block ids too)
//
// Without --shot-at the player spawns normally. Pure parsing, unit-tested.

#include <cstdio>
#include <string>

#include <glm/glm.hpp>

struct ScreenshotOptions {
    static constexpr int SETTLE_FRAMES = 300; // let chunks stream + mesh first

    std::string dir; // empty = capture off
    bool hasPose = false;
    glm::vec3 eye{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    bool night = false;
    int frames = 1;
    int item = 0;

    bool enabled() const { return !dir.empty(); }

    // One option and its value; false if the value is malformed.
    bool parse(const std::string& opt, const std::string& value) {
        if (opt == "--screenshot") {
            dir = value;
            return !dir.empty();
        }
        if (opt == "--shot-at") {
            char tail = 0;
            hasPose = std::sscanf(value.c_str(), "%f,%f,%f,%f,%f%c", &eye.x, &eye.y, &eye.z, &yaw, &pitch, &tail) == 5;
            return hasPose;
        }
        if (opt == "--shot-time") {
            night = value == "night";
            return value == "day" || value == "night";
        }
        if (opt == "--shot-frames") return parseInt(value, 1, 2000, frames);
        if (opt == "--shot-item") return parseInt(value, 0, 65535, item);
        return false;
    }

  private:
    static bool parseInt(const std::string& v, int lo, int hi, int& out) {
        char tail = 0;
        int n = 0;
        if (std::sscanf(v.c_str(), "%d%c", &n, &tail) != 1 || n < lo || n > hi) return false;
        out = n;
        return true;
    }
};
