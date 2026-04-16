#pragma once

#include <string>
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

    std::string chunkFilePath(int x, int z) const;
    void ensureDirectories();
};
