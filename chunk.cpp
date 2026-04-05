/**
 * @file chunk.cpp
 */

#include "chunk.h"
#include "texture_array.h"
#include "profiler.h"
#include <glad/glad.h>
#include <random>

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

    // --- Tree placement ---
    // Deterministic per-chunk PRNG seeded from chunk coordinates
    uint64_t treeSeed = static_cast<uint64_t>(chunkX) * 73856093ULL
                      ^ static_cast<uint64_t>(chunkY) * 19349663ULL;
    std::mt19937 rng(static_cast<unsigned int>(treeSeed));
    std::uniform_int_distribution<int> posDist(2, CHUNK_SIZE - 3); // [2..13]
    std::uniform_int_distribution<int> chanceDist(0, 99);

    constexpr int TREE_ATTEMPTS = 3;
    constexpr int TREE_CHANCE = 40; // percent
    constexpr int WATER_LEVEL = CHUNK_SIZE / 2; // 8

    for (int t = 0; t < TREE_ATTEMPTS; t++) {
        int tx = posDist(rng);
        int tz = posDist(rng);
        int roll = chanceDist(rng);
        if (roll >= TREE_CHANCE) continue;

        int surface = heights[tx][tz];
        if (surface <= WATER_LEVEL) continue;
        if (surface + 5 >= CHUNK_SIZE) continue;

        Cube* surfaceBlock = getBlock(tx, surface, tz);
        if (!surfaceBlock || surfaceBlock->getType() != GRASS) continue;

        // Trunk: 3 blocks
        for (int y = surface + 1; y <= surface + 3; y++) {
            Cube* b = getBlock(tx, y, tz);
            if (b) b->setType(WOOD);
        }

        // Leaves: 3x3 at y=surface+3 and y=surface+4
        for (int ly = surface + 3; ly <= surface + 4; ly++) {
            for (int dx = -1; dx <= 1; dx++) {
                for (int dz = -1; dz <= 1; dz++) {
                    if (dx == 0 && dz == 0 && ly == surface + 3) continue; // trunk
                    Cube* b = getBlock(tx + dx, ly, tz + dz);
                    if (b && b->getType() == AIR) b->setType(LEAVES);
                }
            }
        }

        // Crown
        Cube* crown = getBlock(tx, surface + 5, tz);
        if (crown && crown->getType() == AIR) crown->setType(LEAVES);
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
    double _buildStart = glfwGetTime();

    // Greedy meshing: merge coplanar same-type adjacent faces into larger quads.
    // Vertex layout: pos(3) + texcoord(2) + normal(3) + texLayer(1) = 9 floats/vertex.

    struct FaceDef {
        int d, u, v;              // axis indices for normal, first tangent, second tangent
        int d_sign, u_sign, v_sign; // direction signs (±1) for correct winding order
    };
    static constexpr FaceDef FACE_DEFS[6] = {
        {2, 0, 1,  1,  1,  1},  // Front +Z
        {2, 0, 1, -1, -1,  1},  // Back  -Z
        {0, 2, 1, -1,  1,  1},  // Left  -X
        {0, 2, 1,  1, -1,  1},  // Right +X
        {1, 0, 2,  1,  1, -1},  // Top   +Y
        {1, 0, 2, -1,  1,  1},  // Bottom -Y
    };

    constexpr int MAX_FACES = CHUNK_SIZE * CHUNK_SIZE * 6;
    std::vector<float> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * 9);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(CHUNK_SIZE * CHUNK_SIZE * 4 * 9);
    waterIdx.reserve(CHUNK_SIZE * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

    int mask[CHUNK_SIZE][CHUNK_SIZE];
    const float worldOff[3] = {(float)(chunkX * CHUNK_SIZE), 0.0f, (float)(chunkY * CHUNK_SIZE)};

    for (int f = 0; f < 6; f++) {
        const FaceDef& fd = FACE_DEFS[f];
        const glm::vec3& norm = FACE_NORMALS[f];

        for (int d = 0; d < CHUNK_SIZE; d++) {

            // 1. Build mask for this face direction and slice
            for (int u = 0; u < CHUNK_SIZE; u++) {
                for (int v = 0; v < CHUNK_SIZE; v++) {
                    int c[3]; c[fd.d] = d; c[fd.u] = u; c[fd.v] = v;
                    block_type bt = getBlock(c[0], c[1], c[2])->getType();
                    if (bt == AIR || (bt == WATER && f != 4)) { mask[u][v] = -1; continue; }

                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    Cube* nb = getBlockCross(this, nc[0], nc[1], nc[2], nx_neg, nx_pos, nz_neg, nz_pos);
                    block_type nbType = nb ? nb->getType() : AIR;
                    bool isWater = (bt == WATER);
                    mask[u][v] = (!nb || nbType == AIR || (nbType == WATER && !isWater))
                                 ? (int)bt : -1;
                }
            }

            // 2. Greedy sweep: find maximal rectangles of same type
            for (int u = 0; u < CHUNK_SIZE; u++) {
                for (int v = 0; v < CHUNK_SIZE; ) {
                    int bt = mask[u][v];
                    if (bt == -1) { v++; continue; }

                    int h = 1;
                    while (v + h < CHUNK_SIZE && mask[u][v + h] == bt) h++;
                    int w = 1;
                    while (u + w < CHUNK_SIZE) {
                        bool ok = true;
                        for (int dv = 0; dv < h && ok; dv++)
                            ok = (mask[u + w][v + dv] == bt);
                        if (!ok) break;
                        w++;
                    }

                    float layer = (float)TextureArray::layerForFace((block_type)bt, f);
                    float d_val = (float)d + fd.d_sign * 0.5f;
                    float u_lo = (float)u - 0.5f, u_hi = (float)(u + w) - 0.5f;
                    float v_lo = (float)v - 0.5f, v_hi = (float)(v + h) - 0.5f;
                    float u0 = fd.u_sign > 0 ? u_lo : u_hi;
                    float u1 = fd.u_sign > 0 ? u_hi : u_lo;
                    float v0 = fd.v_sign > 0 ? v_lo : v_hi;
                    float v1 = fd.v_sign > 0 ? v_hi : v_lo;

                    float vp[4][3];
                    vp[0][fd.d]=d_val; vp[0][fd.u]=u0; vp[0][fd.v]=v0;
                    vp[1][fd.d]=d_val; vp[1][fd.u]=u0; vp[1][fd.v]=v1;
                    vp[2][fd.d]=d_val; vp[2][fd.u]=u1; vp[2][fd.v]=v1;
                    vp[3][fd.d]=d_val; vp[3][fd.u]=u1; vp[3][fd.v]=v0;

                    const float uvs[4][2] = {
                        {0.f,0.f}, {0.f,(float)h}, {(float)w,(float)h}, {(float)w,0.f}
                    };

                    bool isWater = (bt == (int)WATER);
                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx   = isWater ? waterIdx   : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    for (int vi = 0; vi < 4; vi++) {
                        verts.push_back(vp[vi][0] + worldOff[0]);
                        verts.push_back(vp[vi][1] + worldOff[1]);
                        verts.push_back(vp[vi][2] + worldOff[2]);
                        verts.push_back(uvs[vi][0]);
                        verts.push_back(uvs[vi][1]);
                        verts.push_back(norm.x);
                        verts.push_back(norm.y);
                        verts.push_back(norm.z);
                        verts.push_back(layer);
                    }
                    idx.push_back(base); idx.push_back(base+1); idx.push_back(base+2);
                    idx.push_back(base+2); idx.push_back(base+3); idx.push_back(base);
                    base += 4;

                    for (int du = 0; du < w; du++)
                        for (int dv = 0; dv < h; dv++)
                            mask[u + du][v + dv] = -1;
                    v += h;
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

    g_frame.meshBuildMs += (glfwGetTime() - _buildStart) * 1000.0;
    g_frame.meshBuilds++;
}

std::vector<Cube*> Chunk::render(Shader shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    if (meshDirty) buildMesh(nx_neg, nx_pos, nz_neg, nz_pos);

    if (opaqueIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, opaqueIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        g_frame.opaqueTriangles += opaqueIndexCount / 3;
        g_frame.vertexCount += opaqueIndexCount / 6 * 4; // 4 verts per 6 indices (quad)
        g_frame.opaqueDrawCalls++;
    }
    return {};
}

void Chunk::renderWater(Shader shaderProgram, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    if (waterIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT,
                       (void*)waterIndexOffset);
        glBindVertexArray(0);
        g_frame.waterTriangles += waterIndexCount / 3;
        g_frame.waterDrawCalls++;
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
