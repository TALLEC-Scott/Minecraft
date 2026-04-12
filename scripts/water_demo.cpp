// water_demo.cpp — 2D cross-section visualizer for the Minecraft water
// cellular automaton used by WaterSimulator.cpp. Replays the same three
// rules (gravity → horizontal spread → decay) on an XY slice using the
// same WATER_FALLING_FLAG encoding, and dumps each tick as a binary PPM
// frame. Converted to MP4 externally with ffmpeg.
//
// Build: g++ -O2 -std=c++20 scripts/water_demo.cpp -o build/water_demo
// Run:   ./build/water_demo <output_dir>
//
// The water_level byte has the same layout as the real simulator:
//   bits 0-2: flow level (0-7)
//   bit   7 : falling flag
//   level=0, flag=0 → source (never decays)
//   level=0, flag=1 → falling water cell (disappears if chain broken)
//   level=1..7, flag=0 → flowing water (decays without support)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static constexpr int W = 64;
static constexpr int H = 36;
static constexpr int CELL_PX = 10;

static constexpr uint8_t LEVEL_MASK = 0x07;
static constexpr uint8_t FALLING_FLAG = 0x80;

enum Cell : uint8_t { C_AIR = 0, C_STONE = 1, C_WATER = 2 };

struct World2D {
    Cell type[H][W]{};
    uint8_t waterRaw[H][W]{}; // level | flags

    Cell get(int x, int y) const {
        if (x < 0 || x >= W || y < 0 || y >= H) return C_STONE;
        return type[y][x];
    }
    uint8_t getRaw(int x, int y) const {
        if (x < 0 || x >= W || y < 0 || y >= H) return 0;
        return waterRaw[y][x];
    }
    void setWater(int x, int y, uint8_t raw) {
        type[y][x] = C_WATER;
        waterRaw[y][x] = raw;
    }
    void setAir(int x, int y) {
        type[y][x] = C_AIR;
        waterRaw[y][x] = 0;
    }
    void setStone(int x, int y) {
        type[y][x] = C_STONE;
        waterRaw[y][x] = 0;
    }
    void placeSource(int x, int y) { setWater(x, y, 0); } // level 0, no flag = source
};

static uint8_t flowLevel(uint8_t raw) { return raw & LEVEL_MASK; }
static bool isFalling(uint8_t raw) { return (raw & FALLING_FLAG) != 0; }
static bool isSource(uint8_t raw) { return (raw & (LEVEL_MASK | FALLING_FLAG)) == 0; }

static int tick(World2D& w) {
    // Snapshot all current water cells.
    std::vector<std::pair<int, int>> active;
    active.reserve(W * H / 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (w.type[y][x] == C_WATER) active.emplace_back(x, y);

    // Process top-to-bottom so gravity settles consistently in a single tick.
    std::sort(active.begin(), active.end(),
              [](auto& a, auto& b) { return a.second < b.second; });

    int changed = 0;

    // Pre-pass: determine which flowing cells lose support this tick.
    // They skip Rule 1/2 in the main pass so they don't backfill
    // positions the previous tick just cleared — that was the
    // checkerboard. y grows DOWNWARD in this 2D sim, so "above" = y-1.
    bool decaying[H][W] = {};
    std::vector<std::pair<int, int>> flowingDecays;
    {
        static const int HDIRS[2] = {-1, 1};
        for (auto [x, y] : active) {
            uint8_t raw = w.waterRaw[y][x];
            if (isSource(raw) || isFalling(raw)) continue;
            uint8_t lvl = flowLevel(raw);
            if (y - 1 >= 0 && w.type[y - 1][x] == C_WATER) {
                uint8_t araw = w.waterRaw[y - 1][x];
                if (isSource(araw) || isFalling(araw)) continue;
                if (flowLevel(araw) < lvl) continue;
            }
            bool supported = false;
            for (int dx : HDIRS) {
                int nx = x + dx;
                if (nx < 0 || nx >= W) continue;
                if (w.type[y][nx] != C_WATER) continue;
                uint8_t nraw = w.waterRaw[y][nx];
                if (isFalling(nraw)) { supported = true; break; }
                if (flowLevel(nraw) < lvl) { supported = true; break; }
            }
            if (!supported) {
                decaying[y][x] = true;
                flowingDecays.emplace_back(x, y);
            }
        }
    }

    for (auto [x, y] : active) {
        if (decaying[y][x]) continue;
        if (w.type[y][x] != C_WATER) continue;
        uint8_t raw = w.waterRaw[y][x];
        uint8_t lvl = flowLevel(raw);
        bool falling = isFalling(raw);
        bool source = isSource(raw);

        // Rule 1: gravity — flow into AIR directly below.
        if (y + 1 < H && w.type[y + 1][x] == C_AIR) {
            w.setWater(x, y + 1, FALLING_FLAG); // falling, level 0
            changed++;
            continue;
        }

        // A water cell can spread horizontally only if it has landed —
        // i.e. below is solid, the world floor, or a non-falling water
        // cell (pool surface). Air or falling-water below means we're
        // over a cliff / in mid-column and must not fan out sideways.
        bool landed = false;
        if (y + 1 >= H) {
            landed = true; // floor of world
        } else {
            Cell below = w.type[y + 1][x];
            if (below == C_AIR) {
                landed = false;
            } else if (below == C_WATER) {
                landed = !isFalling(w.waterRaw[y + 1][x]);
            } else {
                landed = true; // stone
            }
        }

        // Rule 2: horizontal spread. Falling cells act as effective level 0
        // when they land (so newly-landed water fans out like a source).
        uint8_t spreadLevel = falling ? 0 : lvl;
        if (spreadLevel < 7 && landed) {
            static const int dx[2] = {-1, 1};
            for (int i = 0; i < 2; i++) {
                int nx = x + dx[i];
                if (nx < 0 || nx >= W) continue;
                if (w.type[y][nx] == C_AIR) {
                    w.setWater(nx, y, spreadLevel + 1);
                    changed++;
                } else if (w.type[y][nx] == C_WATER) {
                    uint8_t nraw = w.waterRaw[y][nx];
                    if (isSource(nraw) || isFalling(nraw)) continue;
                    if (flowLevel(nraw) > spreadLevel + 1) {
                        w.waterRaw[y][nx] = spreadLevel + 1;
                        changed++;
                    }
                }
            }
        }

        // Rule 3 (falling only): flowing decay was decided in the pre-pass.
        if (falling) {
            bool aboveIsWater = (y - 1 >= 0 && w.type[y - 1][x] == C_WATER);
            if (!aboveIsWater) {
                w.setAir(x, y);
                changed++;
            }
        }
    }

    // Apply pre-pass flowing decays (ripple step — outer ring clears).
    for (auto [fx, fy] : flowingDecays) {
        w.setAir(fx, fy);
        changed++;
    }
    return changed;
}

static void putPixel(uint8_t* buf, int px, int py, int pitch, uint8_t r, uint8_t g, uint8_t b) {
    int i = (py * pitch + px) * 3;
    buf[i + 0] = r;
    buf[i + 1] = g;
    buf[i + 2] = b;
}

static void writeFrame(const World2D& w, const std::string& path) {
    const int pxW = W * CELL_PX;
    const int pxH = H * CELL_PX;
    std::vector<uint8_t> img(static_cast<size_t>(pxW) * pxH * 3, 0);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t r, g, b;
            Cell c = w.type[y][x];
            if (c == C_STONE) {
                float t = 1.0f - (float)y / H * 0.3f;
                r = (uint8_t)(96 * t);
                g = (uint8_t)(96 * t);
                b = (uint8_t)(104 * t);
            } else if (c == C_AIR) {
                float t = (float)y / H;
                r = (uint8_t)(135 + (200 - 135) * t);
                g = (uint8_t)(206 + (225 - 206) * t);
                b = (uint8_t)(250 + (255 - 250) * t);
            } else {
                uint8_t raw = w.waterRaw[y][x];
                uint8_t lvl = flowLevel(raw);
                bool falling = isFalling(raw);
                bool source = isSource(raw);

                // Falling cells render at source opacity (they behave as level 0)
                float alpha = falling ? 0.95f : (1.0f - (lvl / 8.5f));
                uint8_t wr = 30, wg = 100, wb = 220;
                if (source) { wr = 10; wg = 40; wb = 160; }
                else if (falling) { wr = 40; wg = 120; wb = 230; }

                float t = (float)y / H;
                uint8_t sr = (uint8_t)(135 + (200 - 135) * t);
                uint8_t sg = (uint8_t)(206 + (225 - 206) * t);
                uint8_t sb = (uint8_t)(250 + (255 - 250) * t);
                r = (uint8_t)(wr * alpha + sr * (1.0f - alpha));
                g = (uint8_t)(wg * alpha + sg * (1.0f - alpha));
                b = (uint8_t)(wb * alpha + sb * (1.0f - alpha));
            }

            for (int py = 0; py < CELL_PX; py++)
                for (int px = 0; px < CELL_PX; px++)
                    putPixel(img.data(), x * CELL_PX + px, y * CELL_PX + py, pxW, r, g, b);

            if (c != C_AIR) {
                for (int k = 0; k < CELL_PX; k++) {
                    putPixel(img.data(), x * CELL_PX + k, y * CELL_PX, pxW,
                             (uint8_t)(r * 0.8), (uint8_t)(g * 0.8), (uint8_t)(b * 0.8));
                    putPixel(img.data(), x * CELL_PX, y * CELL_PX + k, pxW,
                             (uint8_t)(r * 0.8), (uint8_t)(g * 0.8), (uint8_t)(b * 0.8));
                }
            }
        }
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", pxW, pxH);
    fwrite(img.data(), 1, img.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    const char* outDir = (argc >= 2) ? argv[1] : "docs/water_demo/frames";

    World2D w;
    for (int x = 0; x < W; x++)
        for (int y = H - 4; y < H; y++) w.setStone(x, y);

    // Central plateau: source sits on top
    for (int x = 26; x < 38; x++)
        for (int y = 10; y < 14; y++) w.setStone(x, y);

    // Left stepped shelf so the left cascade has something to land on partway
    for (int x = 14; x < 24; x++)
        for (int y = 18; y < 22; y++) w.setStone(x, y);

    // Basin on the right
    for (int x = 44; x < 58; x++)
        for (int y = H - 4; y < H; y++) w.setAir(x, y);
    for (int x = 44; x < 58; x++) w.setStone(x, H - 1);
    for (int y = H - 8; y < H; y++) {
        w.setStone(44, y);
        w.setStone(58, y);
    }

    // Small bump on the floor
    for (int x = 40; x < 42; x++)
        for (int y = H - 7; y < H - 4; y++) w.setStone(x, y);

    int frame = 0;
    auto framePath = [&]() {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s/frame_%04d.ppm", outDir, frame);
        return std::string(buf);
    };

    // Terrain hold
    for (int i = 0; i < 3; i++) { writeFrame(w, framePath()); frame++; }

    // Place source
    int srcX = 32, srcY = 9;
    w.placeSource(srcX, srcY);
    writeFrame(w, framePath()); frame++;

    // Flow phase
    for (int t = 0; t < 55; t++) {
        tick(w);
        writeFrame(w, framePath());
        frame++;
    }

    // Freeze
    for (int i = 0; i < 3; i++) { writeFrame(w, framePath()); frame++; }

    // Remove source — Rule 3 kicks in
    w.setAir(srcX, srcY);
    writeFrame(w, framePath()); frame++;

    // Drain phase
    for (int t = 0; t < 40; t++) {
        tick(w);
        writeFrame(w, framePath());
        frame++;
    }

    // Final freeze
    for (int i = 0; i < 3; i++) { writeFrame(w, framePath()); frame++; }

    fprintf(stderr, "wrote %d frames to %s\n", frame, outDir);
    return 0;
}
