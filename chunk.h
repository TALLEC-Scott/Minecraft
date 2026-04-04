/**
 * @file chunk.h
 * @brief Defines the Chunk class, representing a chunk of a 3D world.
 */

#pragma once

#include <cstring>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "cube.h"
#include "shader.h"
#include "TerrainGenerator.h"

class Chunk {
public:
    Chunk()
    {
        blocks = new Cube[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
        chunkX = -1;
        chunkY = -1;
    }

    Chunk(int chunkX, int chunkY, TerrainGenerator& terrainGenerator);

    // Move only — Cube GL buffers (now chunk-level) must not be double-freed
    Chunk(Chunk&& other) noexcept
        : blocks(other.blocks), chunkX(other.chunkX), chunkY(other.chunkY),
          chunkVAO(other.chunkVAO), chunkVBO(other.chunkVBO), chunkEBO(other.chunkEBO),
          opaqueIndexCount(other.opaqueIndexCount),
          waterIndexCount(other.waterIndexCount),
          waterIndexOffset(other.waterIndexOffset),
          meshDirty(other.meshDirty)
    {
        std::memcpy(heights, other.heights, sizeof(heights));
        other.blocks = nullptr;
        other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
    }

    Chunk& operator=(Chunk&& other) noexcept {
        if (this != &other) {
            delete[] blocks;
            if (chunkVAO) glDeleteVertexArrays(1, &chunkVAO);
            if (chunkVBO) glDeleteBuffers(1, &chunkVBO);
            if (chunkEBO) glDeleteBuffers(1, &chunkEBO);

            blocks = other.blocks;
            chunkX = other.chunkX;
            chunkY = other.chunkY;
            chunkVAO = other.chunkVAO;
            chunkVBO = other.chunkVBO;
            chunkEBO = other.chunkEBO;
            opaqueIndexCount = other.opaqueIndexCount;
            waterIndexCount = other.waterIndexCount;
            waterIndexOffset = other.waterIndexOffset;
            meshDirty = other.meshDirty;
            std::memcpy(heights, other.heights, sizeof(heights));

            other.blocks = nullptr;
            other.chunkVAO = other.chunkVBO = other.chunkEBO = 0;
        }
        return *this;
    }

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    void buildMesh();
    std::vector<Cube*> render(Shader shaderProgram);
    void renderWater(Shader shaderProgram);
    void destroy();
    void destroyBlock(int x, int y, int z);

    int getLocalHeight(int x, int y);
    int getGlobalHeight(int x, int y);
    Cube* getBlock(int i, int j, int k);

    ~Chunk();

private:
    Cube* blocks = nullptr;
    int heights[CHUNK_SIZE][CHUNK_SIZE]{};
    int chunkX = -1;
    int chunkY = -1;

    // Chunk-level GPU mesh
    GLuint chunkVAO = 0;
    GLuint chunkVBO = 0;
    GLuint chunkEBO = 0;
    int opaqueIndexCount = 0;
    int waterIndexCount = 0;
    size_t waterIndexOffset = 0;
    bool meshDirty = true;
};
