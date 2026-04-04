/**
 * @file chunk.cpp
 */

#include "chunk.h"
#include "texture_array.h"
#include <glad/glad.h>

// Face definitions: 4 vertices per face, each vertex is (dx, dy, dz)
// Order: Front(+Z), Back(-Z), Left(-X), Right(+X), Top(+Y), Bottom(-Y)
static const glm::vec3 FACE_NORMALS[6] = {
    { 0,  0,  1}, // Front
    { 0,  0, -1}, // Back
    {-1,  0,  0}, // Left
    { 1,  0,  0}, // Right
    { 0,  1,  0}, // Top
    { 0, -1,  0}, // Bottom
};

static const glm::vec3 FACE_VERTS[6][4] = {
    // Front (+Z)
    {{-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}},
    // Back (-Z)
    {{ 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}},
    // Left (-X)
    {{-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}},
    // Right (+X)
    {{ 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}},
    // Top (+Y)
    {{-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}},
    // Bottom (-Y)
    {{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}},
};

static const glm::vec2 FACE_UVS[4] = {
    {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}
};

// Neighbor offsets per face (di, dj, dk)
static const int FACE_NEIGHBORS[6][3] = {
    { 0,  0,  1}, // Front
    { 0,  0, -1}, // Back
    {-1,  0,  0}, // Left
    { 1,  0,  0}, // Right
    { 0,  1,  0}, // Top
    { 0, -1,  0}, // Bottom
};

Chunk::Chunk(int chunkX, int chunkY, TerrainGenerator& terrain) {
    blocks = new Cube[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
    this->chunkX = chunkX;
    this->chunkY = chunkY;

    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int j = 0; j < CHUNK_SIZE; j++) {
            for (int k = 0; k < CHUNK_SIZE; k++) {
                int globalX = chunkX * CHUNK_SIZE + i;
                int globalY = chunkY * CHUNK_SIZE + k;

                int height = terrain.getHeight(globalX, globalY);
                this->heights[i][k] = height;

                Cube* block = &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];

                int limit_grass = (int)(0.95 * height) < 1 ? height - 1 : (int)(0.95 * height);
                int limit_stone = (int)(0.7 * height) < 1 ? height - 2 : (int)(0.7 * height);

                double detailNoise = terrain.getNoise(globalX, globalY, j);
                int limit_water = (int)(0.5 * CHUNK_SIZE);

                if (j > height) {
                    block->setType(j == limit_water ? WATER : AIR);
                } else {
                    // Beach detection: check 4 horizontal neighbors
                    bool beach = false;
                    const int dirs[4][2] = {{0,1},{0,-1},{-1,0},{1,0}};
                    for (auto& d : dirs) {
                        Cube* nb = getBlock(i + d[0], j, k + d[1]);
                        if (nb && nb->getType() == WATER) { beach = true; break; }
                    }

                    if (j == 0)
                        block->setType(BEDROCK);
                    else if (j == limit_water && beach)
                        block->setType(SAND);
                    else if (j < limit_stone)
                        block->setType((detailNoise < 0.5 && detailNoise > 0.45) ? COAL_ORE : STONE);
                    else if (j < limit_grass)
                        block->setType(DIRT);
                    else
                        block->setType(GRASS);
                }
            }
        }
    }
}

Cube* Chunk::getBlock(int i, int j, int k) {
    if (i < 0 || i >= CHUNK_SIZE || j < 0 || j >= CHUNK_SIZE || k < 0 || k >= CHUNK_SIZE)
        return nullptr;
    return &blocks[i * CHUNK_SIZE * CHUNK_SIZE + j * CHUNK_SIZE + k];
}

// Returns the block at local (i,j,k), crossing into a neighbor chunk if needed.
// Returns nullptr when out of vertical bounds or when the neighbor chunk isn't loaded.
static Cube* getBlockCross(Chunk* self, int i, int j, int k,
                            Chunk* nx_neg, Chunk* nx_pos,
                            Chunk* nz_neg, Chunk* nz_pos) {
    if (i < 0)           return nx_neg ? nx_neg->getBlock(CHUNK_SIZE - 1, j, k) : nullptr;
    if (i >= CHUNK_SIZE) return nx_pos ? nx_pos->getBlock(0,              j, k) : nullptr;
    if (k < 0)           return nz_neg ? nz_neg->getBlock(i, j, CHUNK_SIZE - 1) : nullptr;
    if (k >= CHUNK_SIZE) return nz_pos ? nz_pos->getBlock(i, j, 0)              : nullptr;
    return self->getBlock(i, j, k);
}

void Chunk::buildMesh(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    // Vertex layout: pos(3) + texcoord(2) + normal(3) + texLayer(1) = 9 floats
    // Positions are chunk-local (0.0–15.5), decoded in shader as local + chunkOffset
    constexpr int MAX_BLOCKS = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
    constexpr int MAX_FACES  = MAX_BLOCKS * 6;
    constexpr int FLOATS_PER_FACE = 4 * 9; // 4 verts × 9 floats
    constexpr int INDICES_PER_FACE = 6;

    std::vector<float> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * FLOATS_PER_FACE);
    opaqueIdx.reserve(MAX_FACES * INDICES_PER_FACE);
    waterVerts.reserve(CHUNK_SIZE * CHUNK_SIZE * FLOATS_PER_FACE);
    waterIdx.reserve(CHUNK_SIZE * CHUNK_SIZE * INDICES_PER_FACE);
    unsigned int opaqueBase = 0, waterBase = 0;

    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int j = 0; j < CHUNK_SIZE; j++) {
            for (int k = 0; k < CHUNK_SIZE; k++) {
                Cube* block = getBlock(i, j, k);
                block_type type = block->getType();
                if (type == AIR) continue;

                bool isWater = (type == WATER);
                float layer = static_cast<float>(TextureArray::layerForType(type));
                glm::vec3 pos(chunkX * CHUNK_SIZE + i, j, chunkY * CHUNK_SIZE + k);

                for (int f = 0; f < 6; f++) {
                    if (isWater && f != 4) continue;

                    int ni = i + FACE_NEIGHBORS[f][0];
                    int nj = j + FACE_NEIGHBORS[f][1];
                    int nk = k + FACE_NEIGHBORS[f][2];
                    Cube* nb = getBlockCross(this, ni, nj, nk, nx_neg, nx_pos, nz_neg, nz_pos);
                    block_type nbType = nb ? nb->getType() : AIR;

                    bool expose = (nb == nullptr)
                               || (nbType == AIR)
                               || (nbType == WATER && !isWater);
                    if (!expose) continue;

                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx   = isWater ? waterIdx   : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    const glm::vec3& n = FACE_NORMALS[f];
                    for (int v = 0; v < 4; v++) {
                        glm::vec3 p = pos + FACE_VERTS[f][v];
                        verts.push_back(p.x);
                        verts.push_back(p.y);
                        verts.push_back(p.z);
                        verts.push_back(FACE_UVS[v].x);
                        verts.push_back(FACE_UVS[v].y);
                        verts.push_back(n.x);
                        verts.push_back(n.y);
                        verts.push_back(n.z);
                        verts.push_back(layer);
                    }
                    idx.push_back(base+0); idx.push_back(base+1); idx.push_back(base+2);
                    idx.push_back(base+2); idx.push_back(base+3); idx.push_back(base+0);
                    base += 4;
                }
            }
        }
    }

    // Combine into one VBO/EBO (opaque first, then water)
    std::vector<float> allVerts;
    allVerts.insert(allVerts.end(), opaqueVerts.begin(), opaqueVerts.end());
    allVerts.insert(allVerts.end(), waterVerts.begin(), waterVerts.end());

    // Water indices need to be offset by the opaque vertex base
    std::vector<unsigned int> allIdx;
    allIdx.insert(allIdx.end(), opaqueIdx.begin(), opaqueIdx.end());
    for (auto idx_val : waterIdx)
        allIdx.push_back(idx_val + opaqueBase);  // offset water indices past opaque verts

    if (chunkVAO == 0) {
        glGenVertexArrays(1, &chunkVAO);
        glGenBuffers(1, &chunkVBO);
        glGenBuffers(1, &chunkEBO);
    }

    glBindVertexArray(chunkVAO);

    glBindBuffer(GL_ARRAY_BUFFER, chunkVBO);
    glBufferData(GL_ARRAY_BUFFER, allVerts.size() * sizeof(float), allVerts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunkEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIdx.size() * sizeof(unsigned int), allIdx.data(), GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 9 * sizeof(float);
    // layout 0: position (chunk-local, shader adds chunkOffset)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
    glEnableVertexAttribArray(0);
    // layout 1: texcoord
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // layout 2: normal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // layout 3: texLayer
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    opaqueIndexCount = static_cast<int>(opaqueIdx.size());
    waterIndexCount  = static_cast<int>(waterIdx.size());
    waterIndexOffset = opaqueIdx.size() * sizeof(unsigned int);
    meshDirty = false;
}

std::vector<Cube*> Chunk::render(Shader shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    if (meshDirty) buildMesh(nx_neg, nx_pos, nz_neg, nz_pos);

    if (opaqueIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, opaqueIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    return {};
}

void Chunk::renderWater(Shader shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    if (waterIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT,
                       (void*)waterIndexOffset);
        glBindVertexArray(0);
    }
}


void Chunk::destroyBlock(int x, int y, int z) {
    blocks[x * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + z].setType(AIR);
    meshDirty = true;
}

int Chunk::getLocalHeight(int x, int y) {
    return heights[x][y];
}

int Chunk::getGlobalHeight(int x, int y) {
    return heights[x % CHUNK_SIZE][y % CHUNK_SIZE];
}

void Chunk::destroy() {
    if (chunkVAO) { glDeleteVertexArrays(1, &chunkVAO); chunkVAO = 0; }
    if (chunkVBO) { glDeleteBuffers(1, &chunkVBO); chunkVBO = 0; }
    if (chunkEBO) { glDeleteBuffers(1, &chunkEBO); chunkEBO = 0; }
    delete[] blocks;
    blocks = nullptr;
}

Chunk::~Chunk() {
    if (blocks) destroy();
}
