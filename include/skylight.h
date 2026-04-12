#pragma once
// Palette-encoded per-chunk sky+block light storage. One byte per block
// (high nibble = sky, low nibble = block). Most sections are uniform
// (all 0xF0 = open sky, or all 0x00 = underground), so palette encoding
// shrinks the 32KB flat array to ~2–4KB per chunk typically.
//
// For callers that need bulk access (mesh builder, BFS), decompress a
// section — or the whole chunk — into a flat uint8_t[] and operate on
// that, then recompress when done.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "cube.h"

class SkyLightSection {
  public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE; // 4096

    SkyLightSection() : palette{0}, bitsPerBlock(1), data((VOLUME + 63) / 64, 0) {}

    uint8_t get(int x, int y, int z) const { return palette[getIdx(x, y, z)]; }
    void set(int x, int y, int z, uint8_t value) {
        uint16_t pi;
        auto it = std::find(palette.begin(), palette.end(), value);
        if (it != palette.end()) {
            pi = (uint16_t)(it - palette.begin());
        } else {
            palette.push_back(value);
            pi = (uint16_t)(palette.size() - 1);
            uint8_t needed = bitsNeeded(palette.size());
            if (needed > bitsPerBlock) grow(needed);
        }
        setIdx(x, y, z, pi);
    }

    // Fast bulk load/store between this section and a flat 4096-byte buffer
    // (XYZ layout: flat[x*256 + y*16 + z]). Used by mesh builder and BFS.
    void loadFromFlat(const uint8_t* flat) {
        palette.clear();
        uint16_t idx[VOLUME];
        for (int i = 0; i < VOLUME; i++) {
            uint8_t v = flat[i];
            auto it = std::find(palette.begin(), palette.end(), v);
            if (it == palette.end()) { palette.push_back(v); idx[i] = (uint16_t)(palette.size() - 1); }
            else idx[i] = (uint16_t)(it - palette.begin());
        }
        if (palette.empty()) palette.push_back(0);
        bitsPerBlock = bitsNeeded(palette.size());
        int entriesPerWord = 64 / bitsPerBlock;
        data.assign((VOLUME + entriesPerWord - 1) / entriesPerWord, 0);
        for (int i = 0; i < VOLUME; i++) {
            int wordIdx = i / entriesPerWord;
            int bitOff = (i % entriesPerWord) * bitsPerBlock;
            uint64_t mask = (1ULL << bitsPerBlock) - 1;
            data[wordIdx] |= ((uint64_t)(idx[i]) & mask) << bitOff;
        }
    }
    void exportToFlat(uint8_t* flat) const {
        int entriesPerWord = 64 / bitsPerBlock;
        uint64_t mask = (1ULL << bitsPerBlock) - 1;
        for (int i = 0; i < VOLUME; i++) {
            int wordIdx = i / entriesPerWord;
            int bitOff = (i % entriesPerWord) * bitsPerBlock;
            flat[i] = palette[(data[wordIdx] >> bitOff) & mask];
        }
    }

    size_t memoryUsage() const { return palette.size() + data.size() * 8; }

  private:
    std::vector<uint8_t> palette;
    uint8_t bitsPerBlock;
    std::vector<uint64_t> data;

    static uint8_t bitsNeeded(size_t n) {
        if (n <= 1) return 1;
        uint8_t b = 0;
        size_t v = n - 1;
        while (v > 0) { b++; v >>= 1; }
        return b;
    }
    // XZY layout for cache-friendly vertical access
    static int linearIdx(int x, int y, int z) { return x * SIZE * SIZE + z * SIZE + y; }
    uint16_t getIdx(int x, int y, int z) const {
        int i = linearIdx(x, y, z);
        int ew = 64 / bitsPerBlock;
        int wi = i / ew;
        int bo = (i % ew) * bitsPerBlock;
        uint64_t mask = (1ULL << bitsPerBlock) - 1;
        return (uint16_t)((data[wi] >> bo) & mask);
    }
    void setIdx(int x, int y, int z, uint16_t val) {
        int i = linearIdx(x, y, z);
        int ew = 64 / bitsPerBlock;
        int wi = i / ew;
        int bo = (i % ew) * bitsPerBlock;
        uint64_t mask = (1ULL << bitsPerBlock) - 1;
        data[wi] &= ~(mask << bo);
        data[wi] |= ((uint64_t)val & mask) << bo;
    }
    void grow(uint8_t newBits) {
        std::vector<uint16_t> tmp(VOLUME);
        int oldEw = 64 / bitsPerBlock;
        uint64_t oldMask = (1ULL << bitsPerBlock) - 1;
        for (int i = 0; i < VOLUME; i++) {
            int wi = i / oldEw, bo = (i % oldEw) * bitsPerBlock;
            tmp[i] = (uint16_t)((data[wi] >> bo) & oldMask);
        }
        bitsPerBlock = newBits;
        int newEw = 64 / newBits;
        data.assign((VOLUME + newEw - 1) / newEw, 0);
        for (int i = 0; i < VOLUME; i++) {
            int wi = i / newEw, bo = (i % newEw) * newBits;
            data[wi] |= ((uint64_t)tmp[i]) << bo;
        }
    }
};

// 8 sections per chunk (CHUNK_HEIGHT / 16). Flat array layout when
// decompressed: [x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].
class SkyLight {
  public:
    static constexpr int NUM_SECTIONS = CHUNK_HEIGHT / 16;
    static constexpr size_t FLAT_SIZE = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

    uint8_t get(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
        return sections[y / 16].get(x, y % 16, z);
    }
    void set(int x, int y, int z, uint8_t value) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return;
        sections[y / 16].set(x, y % 16, z, value);
    }

    // Decompress the entire chunk's light into a flat uint8_t[FLAT_SIZE] buffer.
    void decompressAll(uint8_t* flat) const {
        // Section layout is XZY (x*256+z*16+y). Chunk flat is XYZ (x*H*S+y*S+z).
        // So we need to transpose per-section output into chunk layout.
        uint8_t sbuf[SkyLightSection::VOLUME];
        for (int s = 0; s < NUM_SECTIONS; s++) {
            sections[s].exportToFlat(sbuf);
            int baseY = s * 16;
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int y = 0; y < 16; y++)
                    for (int z = 0; z < CHUNK_SIZE; z++) {
                        int flatIdx = x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE + z;
                        int sIdx = x * 16 * 16 + z * 16 + y;
                        flat[flatIdx] = sbuf[sIdx];
                    }
        }
    }
    // Compress a flat buffer back into palette form.
    void compressAll(const uint8_t* flat) {
        uint8_t sbuf[SkyLightSection::VOLUME];
        for (int s = 0; s < NUM_SECTIONS; s++) {
            int baseY = s * 16;
            for (int x = 0; x < CHUNK_SIZE; x++)
                for (int y = 0; y < 16; y++)
                    for (int z = 0; z < CHUNK_SIZE; z++) {
                        int flatIdx = x * CHUNK_HEIGHT * CHUNK_SIZE + (baseY + y) * CHUNK_SIZE + z;
                        int sIdx = x * 16 * 16 + z * 16 + y;
                        sbuf[sIdx] = flat[flatIdx];
                    }
            sections[s].loadFromFlat(sbuf);
        }
    }

    size_t memoryUsage() const {
        size_t total = 0;
        for (int s = 0; s < NUM_SECTIONS; s++) total += sections[s].memoryUsage();
        return total;
    }

  private:
    SkyLightSection sections[NUM_SECTIONS];
};
