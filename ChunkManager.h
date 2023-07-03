//
// Created by scott on 03/07/23.
//

#ifndef MINECRAFT_CHUNKMANAGER_H
#define MINECRAFT_CHUNKMANAGER_H

#define RENDER_DISTANCE 1
#define CHUNK_SIZE 15

#include "glm/vec3.hpp"
#include "chunk.h"

struct Vec2Hash {
    std::size_t operator()(const glm::ivec2& vec) const {
        std::hash<int> hasher;
        std::size_t seed = hasher(vec.x);
        seed ^= hasher(vec.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

class ChunkManager {

public:
    ChunkManager(int renderDistance, int chunkSize, TerrainGenerator &terrainGenerator);
    void update(glm::vec3 cameraPosition);
    void render(Shader shaderProgram, World* world);


private:
    const int renderDistance;
    const int chunkSize;
    TerrainGenerator& terrainGenerator;
    std::unordered_map<glm::ivec2, Chunk, Vec2Hash> chunks;

    void loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);

    void unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);
    Chunk* generateChunk(int x, int z);

};


#endif //MINECRAFT_CHUNKMANAGER_H
