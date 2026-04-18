#pragma once

#include <ctime>
#include <string>
#include <unordered_set>
#include <glm/glm.hpp>
#include "cube.h"
#include "player.h"

struct ChunkData;
class Chunk;
class TerrainGenerator;

struct PlayerSaveData {
    glm::vec3 position = glm::vec3(15, 90, 15);
    float yaw = -90.0f;
    float pitch = 0.0f;
    bool walkMode = false;
    block_type hotbar[Player::HOTBAR_SIZE] = {AIR, GRASS, DIRT, STONE, WOOD, SAND, WATER, GLOWSTONE, LEAVES, SNOW};
    int selectedSlot = 0;
};

class WorldSave {
  public:
    explicit WorldSave(const std::string& savePath);

    void saveLevelData(unsigned int seed, const PlayerSaveData& player);
    bool loadLevelData(unsigned int& seed, PlayerSaveData& player);

    // Display-name and last-played metadata. Set before first save on new
    // worlds; load() populates them from level.dat if present.
    void setDisplayName(const std::string& name) { displayName = name; }
    const std::string& getDisplayName() const { return displayName; }
    std::time_t getLastPlayed() const { return lastPlayed; }
    const std::string& getBasePath() const { return basePath; }

    void saveChunk(const Chunk& chunk);
    bool loadChunkData(int chunkX, int chunkZ, ChunkData& out, TerrainGenerator& terrain);
    bool chunkExists(int chunkX, int chunkZ) const;

    // Mount persistent storage (IDBFS on web, no-op on desktop)
    static void mountPersistentStorage();
    // Sync in-memory filesystem to persistent storage (IDBFS on web, no-op on desktop)
    static void syncToDisk();

  private:
    std::string basePath;
    std::string chunksPath;
    std::string displayName;
    std::time_t lastPlayed = 0;
    // In-memory index of saved chunks — avoids filesystem stat() per lookup
    struct ChunkKeyHash {
        std::size_t operator()(std::pair<int, int> k) const {
            std::size_t seed = std::hash<int>()(k.first);
            seed ^= std::hash<int>()(k.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    std::unordered_set<std::pair<int, int>, ChunkKeyHash> savedChunks;

    std::string chunkFilePath(int x, int z) const;
    void ensureDirectories();
    void scanSavedChunks();
};
