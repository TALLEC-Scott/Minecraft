//
// Created by scott on 03/07/23.
//
#pragma once



#include <glm/vec3.hpp>

#include "chunk.h"
#include <unordered_map>

struct Vec2Hash {
    std::size_t operator()(const glm::ivec2& vec) const {
        std::hash<int> hasher;
        std::size_t seed = hasher(vec.x);
        seed ^= hasher(vec.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
class World;
class Chunk;

class ChunkManager {

public:
    ChunkManager(int renderDistance, int chunkSize, TerrainGenerator &terrainGenerator);
    void update(glm::vec3 cameraPosition);
    void render(Shader shaderProgram);
    std::unordered_map<glm::ivec2, Chunk, Vec2Hash> chunks;
    Chunk* getChunk(int chunkX, int chunkZ);


private:
    TerrainGenerator& terrainGenerator;


    void loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);

    void unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk);
    Chunk* generateChunk(int x, int z);

};