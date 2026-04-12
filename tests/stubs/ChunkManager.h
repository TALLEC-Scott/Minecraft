// Test-only minimal ChunkManager. Provides just what WaterSimulator needs:
// getChunk(cx, cz) returning a Chunk* (or nullptr). Chunks can be created
// on demand by test harness code via getOrCreate().
#pragma once

#include <cstdint>
#include <unordered_map>
#include "chunk.h"

class ChunkManager {
  public:
    std::unordered_map<int64_t, Chunk> chunks;

    static int64_t key(int cx, int cz) {
        return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
    }

    Chunk* getChunk(int cx, int cz) {
        auto it = chunks.find(key(cx, cz));
        if (it == chunks.end()) return nullptr;
        return &it->second;
    }

    Chunk& getOrCreate(int cx, int cz) {
        auto [it, inserted] = chunks.try_emplace(key(cx, cz));
        if (inserted) {
            it->second.chunkX = cx;
            it->second.chunkZ = cz;
        }
        return it->second;
    }
};
