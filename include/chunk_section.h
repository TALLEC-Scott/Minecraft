#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "cube.h"

// 16x16x16 section with palette-based packed storage.
// Stores a palette of block types and a packed bit array of indices.
// Empty (all-air) sections can be represented as null pointers by the chunk.
class ChunkSection {
  public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE; // 4096

    ChunkSection()
        : palette{AIR}, bitsPerBlock(1),
          data(bitsPerWord(1) > 0 ? (VOLUME + bitsPerWord(1) - 1) / bitsPerWord(1) : VOLUME, 0) {}

    block_type getBlock(int x, int y, int z) const {
        uint16_t idx = getPaletteIndex(x, y, z);
        return static_cast<block_type>(palette[idx]);
    }

    void setBlock(int x, int y, int z, block_type type) {
        // Find or add type in palette
        uint16_t palIdx = 0;
        auto it = std::find(palette.begin(), palette.end(), static_cast<uint8_t>(type));
        if (it != palette.end()) {
            palIdx = static_cast<uint16_t>(it - palette.begin());
        } else {
            palette.push_back(static_cast<uint8_t>(type));
            palIdx = static_cast<uint16_t>(palette.size() - 1);
            uint8_t needed = bitsNeeded(palette.size());
            if (needed > bitsPerBlock) grow(needed);
        }
        setPaletteIndex(x, y, z, palIdx);
    }

    bool isEmpty() const { return palette.size() == 1 && palette[0] == AIR; }

    // Decompress into a flat Cube array (4096 entries, indexed [x * 16*16 + y * 16 + z])
    void decompress(Cube* out) const {
        for (int x = 0; x < SIZE; x++)
            for (int y = 0; y < SIZE; y++)
                for (int z = 0; z < SIZE; z++) {
                    int flat = x * SIZE * SIZE + y * SIZE + z;
                    uint16_t idx = getPaletteIndex(x, y, z);
                    out[flat].setType(static_cast<block_type>(palette[idx]));
                }
    }

    // Compress from a flat Cube array (4096 entries, same layout)
    void compress(const Cube* source) {
        palette.clear();
        // Build palette
        for (int i = 0; i < VOLUME; i++) {
            uint8_t t = static_cast<uint8_t>(source[i].getType());
            if (std::find(palette.begin(), palette.end(), t) == palette.end()) palette.push_back(t);
        }
        if (palette.empty()) palette.push_back(AIR);
        bitsPerBlock = bitsNeeded(palette.size());
        int entriesPerWord = 64 / bitsPerBlock;
        data.assign((VOLUME + entriesPerWord - 1) / entriesPerWord, 0);
        // Pack indices
        for (int x = 0; x < SIZE; x++)
            for (int y = 0; y < SIZE; y++)
                for (int z = 0; z < SIZE; z++) {
                    uint8_t t = static_cast<uint8_t>(source[x * SIZE * SIZE + y * SIZE + z].getType());
                    auto it = std::find(palette.begin(), palette.end(), t);
                    setPaletteIndex(x, y, z, static_cast<uint16_t>(it - palette.begin()));
                }
    }

    const std::vector<uint8_t>& getPalette() const { return palette; }
    uint8_t getBitsPerBlock() const { return bitsPerBlock; }
    const std::vector<uint64_t>& getPackedData() const { return data; }
    void loadPacked(std::vector<uint8_t> pal, uint8_t bits, std::vector<uint64_t> packed) {
        palette = std::move(pal);
        bitsPerBlock = bits;
        data = std::move(packed);
    }
    size_t memoryUsage() const { return palette.size() + data.size() * 8; }

  private:
    std::vector<uint8_t> palette; // index -> block_type
    uint8_t bitsPerBlock;
    std::vector<uint64_t> data; // packed bit array

    static uint8_t bitsNeeded(size_t paletteSize) {
        if (paletteSize <= 1) return 1;
        uint8_t bits = 0;
        size_t v = paletteSize - 1;
        while (v > 0) {
            bits++;
            v >>= 1;
        }
        return bits;
    }

    static int bitsPerWord(uint8_t bits) { return 64 / bits; }

    // Linear index: x * 256 + z * 16 + y (XZY for cache-friendly vertical column access)
    static int linearIndex(int x, int y, int z) { return x * SIZE * SIZE + z * SIZE + y; }

    uint16_t getPaletteIndex(int x, int y, int z) const {
        int idx = linearIndex(x, y, z);
        int entriesPerWord = 64 / bitsPerBlock;
        int wordIdx = idx / entriesPerWord;
        int bitOffset = (idx % entriesPerWord) * bitsPerBlock;
        uint64_t mask = (1ULL << bitsPerBlock) - 1;
        return static_cast<uint16_t>((data[wordIdx] >> bitOffset) & mask);
    }

    void setPaletteIndex(int x, int y, int z, uint16_t val) {
        int idx = linearIndex(x, y, z);
        int entriesPerWord = 64 / bitsPerBlock;
        int wordIdx = idx / entriesPerWord;
        int bitOffset = (idx % entriesPerWord) * bitsPerBlock;
        uint64_t mask = (1ULL << bitsPerBlock) - 1;
        data[wordIdx] &= ~(mask << bitOffset);
        data[wordIdx] |= (static_cast<uint64_t>(val) & mask) << bitOffset;
    }

    void grow(uint8_t newBits) {
        // Re-pack all entries with the new bit width
        std::vector<uint16_t> indices(VOLUME);
        int oldEntriesPerWord = 64 / bitsPerBlock;
        for (int i = 0; i < VOLUME; i++) {
            int wordIdx = i / oldEntriesPerWord;
            int bitOffset = (i % oldEntriesPerWord) * bitsPerBlock;
            uint64_t mask = (1ULL << bitsPerBlock) - 1;
            indices[i] = static_cast<uint16_t>((data[wordIdx] >> bitOffset) & mask);
        }
        bitsPerBlock = newBits;
        int newEntriesPerWord = 64 / newBits;
        data.assign((VOLUME + newEntriesPerWord - 1) / newEntriesPerWord, 0);
        for (int i = 0; i < VOLUME; i++) {
            int wordIdx = i / newEntriesPerWord;
            int bitOffset = (i % newEntriesPerWord) * newBits;
            uint64_t mask = (1ULL << newBits) - 1;
            data[wordIdx] |= (static_cast<uint64_t>(indices[i]) & mask) << bitOffset;
        }
    }
};
