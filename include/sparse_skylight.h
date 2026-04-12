#pragma once
// Per-section sparse storage for per-block light (sky + block in one byte).
// Each of NUM_SECTIONS sections is either:
//   - Uniform (data == nullptr): every block has the same value (`uniform[s]`)
//   - Mixed (data != nullptr): 4 KB array of 16×16×16 values
//
// Most chunks have ~5 uniform sections (open sky above terrain + deep
// underground) and ~3 mixed sections (gradient near terrain / caves).
// Saves ~20 KB vs the flat 32 KB array on a typical chunk.

#include <cstdint>
#include <cstring>
#include <memory>
#include "cube.h"

class SparseSkyLight {
  public:
    static constexpr int SECTION_VOL = 16 * 16 * 16; // 4096
    static constexpr int NUM_SECTIONS = CHUNK_HEIGHT / 16;

    // Layout: flat[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z]
    // Section layout inside each 4 KB buffer: [x * 16 * 16 + y * 16 + z]
    // where y is the local Y within the section (0..15).

    uint8_t get(int x, int y, int z) const {
        int sy = y / 16;
        int ly = y % 16;
        if (!data[sy]) return uniform[sy];
        return data[sy][x * 16 * 16 + ly * 16 + z];
    }

    void set(int x, int y, int z, uint8_t value) {
        int sy = y / 16;
        int ly = y % 16;
        if (!data[sy]) {
            if (value == uniform[sy]) return; // still uniform
            // Promote to full array
            data[sy] = std::make_unique<uint8_t[]>(SECTION_VOL);
            std::memset(data[sy].get(), uniform[sy], SECTION_VOL);
        }
        data[sy][x * 16 * 16 + ly * 16 + z] = value;
    }

    // Populate the sparse representation from a flat CHUNK_SIZE × CHUNK_HEIGHT
    // × CHUNK_SIZE buffer. Auto-detects uniform sections and frees their data.
    void loadFromFlat(const uint8_t* flat) {
        for (int s = 0; s < NUM_SECTIONS; s++) {
            int baseY = s * 16;
            // First pass: check uniformity
            uint8_t first = flat[0 * CHUNK_HEIGHT * CHUNK_SIZE + baseY * CHUNK_SIZE + 0];
            bool isUniform = true;
            for (int x = 0; x < CHUNK_SIZE && isUniform; x++)
                for (int y = 0; y < 16 && isUniform; y++)
                    for (int z = 0; z < CHUNK_SIZE; z++) {
                        uint8_t v = flat[x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE + z];
                        if (v != first) { isUniform = false; break; }
                    }
            if (isUniform) {
                data[s].reset();
                uniform[s] = first;
            } else {
                if (!data[s]) data[s] = std::make_unique<uint8_t[]>(SECTION_VOL);
                for (int x = 0; x < CHUNK_SIZE; x++)
                    for (int y = 0; y < 16; y++)
                        for (int z = 0; z < CHUNK_SIZE; z++) {
                            data[s][x * 16 * 16 + y * 16 + z] =
                                flat[x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE + z];
                        }
            }
        }
    }

    // Write the sparse representation into a flat CHUNK_SIZE × CHUNK_HEIGHT
    // × CHUNK_SIZE buffer. Caller owns the buffer.
    void exportToFlat(uint8_t* flat) const {
        for (int s = 0; s < NUM_SECTIONS; s++) {
            int baseY = s * 16;
            if (!data[s]) {
                // Uniform: fill 16 YZ×X slices with the single value
                for (int x = 0; x < CHUNK_SIZE; x++)
                    for (int y = 0; y < 16; y++)
                        std::memset(&flat[x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE],
                                    uniform[s], CHUNK_SIZE);
            } else {
                for (int x = 0; x < CHUNK_SIZE; x++)
                    for (int y = 0; y < 16; y++)
                        for (int z = 0; z < CHUNK_SIZE; z++) {
                            flat[x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE + z] =
                                data[s][x * 16 * 16 + y * 16 + z];
                        }
            }
        }
    }

    size_t memoryUsage() const {
        size_t total = sizeof(*this);
        for (int s = 0; s < NUM_SECTIONS; s++)
            if (data[s]) total += SECTION_VOL;
        return total;
    }

  private:
    std::unique_ptr<uint8_t[]> data[NUM_SECTIONS];
    uint8_t uniform[NUM_SECTIONS] = {};
};
