#pragma once
#include <cstdint>
#include <cstddef>
#include "cube.h"

// Packed light: high nibble = sky light (0-15), low nibble = block light (0-15)
// One byte per block instead of two separate arrays.

inline uint8_t packLight(uint8_t sky, uint8_t block) {
    return (sky << 4) | (block & 0xF);
}

inline uint8_t unpackSky(uint8_t packed) {
    return packed >> 4;
}

inline uint8_t unpackBlock(uint8_t packed) {
    return packed & 0xF;
}

inline size_t lightIdx(int x, int y, int z) {
    return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
}

// Read/write helpers for a packed light array
inline uint8_t getSkyFromPacked(const uint8_t* light, int x, int y, int z) {
    return unpackSky(light[lightIdx(x, y, z)]);
}

inline uint8_t getBlockFromPacked(const uint8_t* light, int x, int y, int z) {
    return unpackBlock(light[lightIdx(x, y, z)]);
}

inline void setSkyInPacked(uint8_t* light, int x, int y, int z, uint8_t val) {
    size_t i = lightIdx(x, y, z);
    light[i] = (val << 4) | (light[i] & 0xF);
}

inline void setBlockInPacked(uint8_t* light, int x, int y, int z, uint8_t val) {
    size_t i = lightIdx(x, y, z);
    light[i] = (light[i] & 0xF0) | (val & 0xF);
}
