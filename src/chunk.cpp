/**
 * @file chunk.cpp
 */

#include "chunk.h"
#include "texture_array.h"
#include "profiler.h"
#include "gl_header.h"
#include <random>
#include <algorithm>

// Face definitions: 4 vertices per face, each vertex is (dx, dy, dz)
// Order: Front(+Z), Back(-Z), Left(-X), Right(+X), Top(+Y), Bottom(-Y)
static const glm::vec3 FACE_NORMALS[6] = {
    {0, 0, 1},  // Front
    {0, 0, -1}, // Back
    {-1, 0, 0}, // Left
    {1, 0, 0},  // Right
    {0, 1, 0},  // Top
    {0, -1, 0}, // Bottom
};

static const glm::vec3 FACE_VERTS[6][4] = {
    // Front (+Z)
    {{-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}},
    // Back (-Z)
    {{0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}},
    // Left (-X)
    {{-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}},
    // Right (+X)
    {{0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}},
    // Top (+Y)
    {{-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
    // Bottom (-Y)
    {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}},
};

static const glm::vec2 FACE_UVS[4] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};

// Neighbor offsets per face (di, dj, dk)
static const int FACE_NEIGHBORS[6][3] = {
    {0, 0, 1},  // Front
    {0, 0, -1}, // Back
    {-1, 0, 0}, // Left
    {1, 0, 0},  // Right
    {0, 1, 0},  // Top
    {0, -1, 0}, // Bottom
};

static bool isBlockOpaque(block_type t);
static bool isBlockFiltering(block_type t);
static void computeSkyLightData(Cube* blocks, uint8_t* skyLight, int maxSolidY);

// Helper for block access in ChunkData (same layout as Chunk::getBlock)
static Cube* getBlockFromData(ChunkData& d, int i, int j, int k) {
    if (i < 0 || i >= CHUNK_SIZE || j < 0 || j >= CHUNK_HEIGHT || k < 0 || k >= CHUNK_SIZE) return nullptr;
    return &d.blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
}

ChunkData generateChunkData(int chunkX, int chunkZ, TerrainGenerator& terrain) {
    ChunkData d;
    d.blocks = std::shared_ptr<Cube[]>(new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]);
    d.skyLight = std::shared_ptr<uint8_t[]>(new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]());
    d.chunkX = chunkX;
    d.chunkZ = chunkZ;

    constexpr int WATER_LEVEL = CHUNK_HEIGHT / 2;

    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            int globalX = chunkX * CHUNK_SIZE + i;
            int globalZ = chunkZ * CHUNK_SIZE + k;

            Biome biome;
            int height = terrain.getHeightAndBiome(globalX, globalZ, biome);
            d.heights[i][k] = height;
            d.biomes[i][k] = biome;
            const BiomeParams& bp = terrain.getBiomeParams(biome);

            int limit_stone = std::max(1, (int)(0.7 * height));

            for (int j = 0; j < CHUNK_HEIGHT; j++) {
                Cube* block = &d.blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
                double detailNoise = terrain.getNoise(globalX, globalZ, j);

                if (j > height) {
                    block->setType(j <= WATER_LEVEL ? WATER : AIR);
                } else if (j == 0) {
                    block->setType(BEDROCK);
                } else if (j < limit_stone) {
                    block->setType((detailNoise > 0.45 && detailNoise < 0.5) ? COAL_ORE : STONE);
                } else if (j == height) {
                    // Surface: altitude overrides biome at high elevations
                    int snowLine = WATER_LEVEL + 35;  // ~99
                    int stoneLine = WATER_LEVEL + 15; // ~79
                    if (height >= snowLine && biome == BIOME_TUNDRA)
                        block->setType(SNOW); // snow peaks in cold biomes
                    else if (height >= stoneLine)
                        block->setType(STONE); // exposed rock at high altitude
                    else
                        block->setType(bp.surfaceBlock); // biome surface (grass/sand/snow)
                } else {
                    // Below surface (limit_stone <= j < height): subsurface material
                    block->setType(bp.subsurfaceBlock);
                }
            }
        }
    }

    // --- Shore post-pass: convert blocks adjacent to water ---
    for (int i = 0; i < CHUNK_SIZE; i++) {
        block_type shoreBlock = (d.biomes[i][0] == BIOME_TUNDRA) ? GRAVEL : SAND;
        for (int k = 0; k < CHUNK_SIZE; k++) {
            if (d.biomes[i][k] == BIOME_TUNDRA)
                shoreBlock = GRAVEL;
            else
                shoreBlock = SAND;

            for (int j = 0; j <= WATER_LEVEL + 1 && j < CHUNK_HEIGHT; j++) {
                Cube* block = getBlockFromData(d, i, j, k);
                block_type bt = block->getType();
                if (bt != DIRT && bt != GRASS && bt != STONE && bt != GRAVEL) continue;

                if (j < WATER_LEVEL) {
                    Cube* above = getBlockFromData(d, i, j + 1, k);
                    if (above && above->getType() == WATER) {
                        block->setType(shoreBlock);
                        continue;
                    }
                }
                static const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (auto& dir : dirs) {
                    Cube* nb = getBlockFromData(d, i + dir[0], j + dir[1], k + dir[2]);
                    if (nb && nb->getType() == WATER) {
                        block->setType(shoreBlock);
                        break;
                    }
                }
            }
        }
    }

    // --- Tree placement ---
    // Deterministic per-chunk PRNG seeded from chunk coordinates
    uint64_t treeSeed = static_cast<uint64_t>(chunkX) * 73856093ULL ^ static_cast<uint64_t>(chunkZ) * 19349663ULL;
    std::mt19937 rng(static_cast<unsigned int>(treeSeed));
    std::uniform_int_distribution<int> posDist(3, CHUNK_SIZE - 4);
    std::uniform_int_distribution<int> chanceDist(0, 99);
    std::uniform_int_distribution<int> trunkDist(4, 7);
    std::uniform_int_distribution<int> radiusDist(2, 3);
    std::uniform_int_distribution<int> layersDist(3, 5);

    // Tree density from biome params (checked per tree position for accuracy at borders)

    constexpr int MAX_TREE_ATTEMPTS = 5;
    constexpr int TREE_WATER_LEVEL = CHUNK_HEIGHT / 2;

    for (int t = 0; t < MAX_TREE_ATTEMPTS; t++) {
        int tx = posDist(rng);
        int tz = posDist(rng);

        // Check biome at this tree position for density/chance
        int treeGlobalX = chunkX * CHUNK_SIZE + tx;
        int treeGlobalZ = chunkZ * CHUNK_SIZE + tz;
        Biome treeBiome = terrain.getBiome(treeGlobalX, treeGlobalZ);
        const BiomeParams& tbp = terrain.getBiomeParams(treeBiome);
        if (tbp.treeDensity <= 0.0f) continue;

        // Scale attempts: consume RNG but skip low-density biomes probabilistically
        int roll = chanceDist(rng);
        int effectiveChance = static_cast<int>(tbp.treeChance * tbp.treeDensity);
        if (roll >= effectiveChance) continue;

        int trunkH = trunkDist(rng);
        int radius = radiusDist(rng);
        int layers = layersDist(rng);
        int totalH = trunkH + layers + 1;

        int surface = d.heights[tx][tz];
        if (surface <= TREE_WATER_LEVEL) continue;
        if (surface + totalH >= CHUNK_HEIGHT) continue;
        if (tx - radius < 1 || tx + radius >= CHUNK_SIZE - 1) continue;
        if (tz - radius < 1 || tz + radius >= CHUNK_SIZE - 1) continue;

        Cube* surfaceBlock = getBlockFromData(d, tx, surface, tz);
        if (!surfaceBlock || surfaceBlock->getType() != tbp.surfaceBlock) continue;
        if (tbp.surfaceBlock != GRASS) continue; // only plant trees on grass

        // Slope check: reject if any neighbor height differs by more than 2
        bool tooSteep = false;
        for (int dx = -1; dx <= 1 && !tooSteep; dx++) {
            for (int dz = -1; dz <= 1 && !tooSteep; dz++) {
                if (dx == 0 && dz == 0) continue;
                int nx = tx + dx, nz = tz + dz;
                if (nx >= 0 && nx < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE) {
                    if (std::abs(d.heights[nx][nz] - surface) > 2) tooSteep = true;
                }
            }
        }
        if (tooSteep) continue;

        // Trunk
        for (int y = surface + 1; y <= surface + trunkH; y++) {
            Cube* b = getBlockFromData(d, tx, y, tz);
            if (b) b->setType(WOOD);
        }

        int canopyBase = surface + trunkH;
        for (int ly = 0; ly < layers; ly++) {
            int layerR = std::max(1, radius - ly / 2);
            for (int dx = -layerR; dx <= layerR; dx++) {
                for (int dz = -layerR; dz <= layerR; dz++) {
                    if (abs(dx) == layerR && abs(dz) == layerR && layerR > 1) continue;
                    if (dx == 0 && dz == 0 && ly == 0) continue;
                    int bx = tx + dx, by = canopyBase + ly, bz = tz + dz;
                    if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
                    Cube* b = getBlockFromData(d, bx, by, bz);
                    if (b && b->getType() == AIR) b->setType(LEAVES);
                }
            }
        }

        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                if (abs(dx) == 1 && abs(dz) == 1) continue;
                int bx = tx + dx, bz = tz + dz;
                if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
                Cube* crown = getBlockFromData(d, bx, canopyBase + layers, bz);
                if (crown && crown->getType() == AIR) crown->setType(LEAVES);
            }
        }
    }

    // --- Cactus placement ---
    std::uniform_int_distribution<int> cactusDist(2, 4);
    for (int t = 0; t < 3; t++) {
        int cx = posDist(rng);
        int cz = posDist(rng);
        int roll = chanceDist(rng);
        if (roll >= 30) continue;

        if (d.biomes[cx][cz] != BIOME_DESERT) continue;

        int surface = d.heights[cx][cz];
        if (surface <= WATER_LEVEL) continue;
        int cactusH = cactusDist(rng);
        if (surface + cactusH >= CHUNK_HEIGHT) continue;

        Cube* surfBlock = getBlockFromData(d, cx, surface, cz);
        if (!surfBlock || surfBlock->getType() != SAND) continue;

        for (int y = surface + 1; y <= surface + cactusH; y++) {
            Cube* b = getBlockFromData(d, cx, y, cz);
            if (b && b->getType() == AIR) b->setType(CACTUS);
        }
    }

    // Compute maxSolidY
    d.maxSolidY = 0;
    for (int j = CHUNK_HEIGHT - 1; j >= 0; j--) {
        bool found = false;
        for (int i = 0; i < CHUNK_SIZE && !found; i++)
            for (int k = 0; k < CHUNK_SIZE && !found; k++)
                if (getBlockFromData(d, i, j, k)->getType() != AIR) {
                    d.maxSolidY = j;
                    found = true;
                }
        if (found) break;
    }

    computeSkyLightData(d.blocks.get(), d.skyLight.get(), d.maxSolidY);
    return d;
}

// Existing constructor now delegates to generateChunkData
Chunk::Chunk(int chunkX, int chunkY, TerrainGenerator& terrain)
    : Chunk(generateChunkData(chunkX, chunkY, terrain)) {}

// Construct from pre-generated data (no computation, main thread only)
Chunk::Chunk(ChunkData&& data) {
    blocks = std::move(data.blocks);
    skyLight = std::move(data.skyLight);
    this->chunkX = data.chunkX;
    this->chunkY = data.chunkZ;
    maxSolidY = data.maxSolidY;
    std::memcpy(heights, data.heights, sizeof(heights));
    std::memcpy(biomes, data.biomes, sizeof(biomes));
    meshDirty = true;
}

static bool isBlockOpaque(block_type t) {
    return hasFlag(t, BF_OPAQUE);
}

static bool isBlockFiltering(block_type t) {
    return hasFlag(t, BF_FILTERING);
}

// Free function: compute sky light on raw arrays (safe for worker threads)
static void computeSkyLightData(Cube* blocks, uint8_t* skyLight, int maxSolidY) {
    const size_t total = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
    std::memset(skyLight, 0, total);

    auto slIdx = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
    };

    // Pre-compute opacity cache to avoid getType()+hasFlag() in the hot BFS loop
    // Pack opaque (bit 0) and filtering (bit 1) into one byte per block
    const int scanH = std::min(maxSolidY + 16, CHUNK_HEIGHT); // only need to scan near terrain
    std::vector<uint8_t> blockInfo(CHUNK_SIZE * scanH * CHUNK_SIZE);
    auto biIdx = [&](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) * scanH * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
    };
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < scanH; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                block_type bt = blocks[slIdx(x, y, z)].getType();
                blockInfo[biIdx(x, y, z)] = (isBlockOpaque(bt) ? 1 : 0) | (isBlockFiltering(bt) ? 2 : 0);
            }

    // Phase 1: vertical ray — light starts at 15, reduced by 1 per filtering block
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint8_t light = 15;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                if (y < scanH) {
                    uint8_t info = blockInfo[biIdx(x, y, z)];
                    if (info & 1) break; // opaque
                    if (info & 2) light = (light > 1) ? light - 1 : 0; // filtering
                }
                skyLight[slIdx(x, y, z)] = light;
            }
        }
    }

    // Phase 2: BFS flood fill — only seed from lit blocks at shadow edges
    // Use packed int32 queue instead of tuple for cache efficiency
    std::vector<int32_t> queue;
    queue.reserve(CHUNK_SIZE * CHUNK_SIZE * 2);
    auto packCoord = [](int x, int y, int z) -> int32_t {
        return (x << 20) | (y << 8) | z;
    };

    // Only seed blocks that are lit AND have at least one dark/unlit neighbor
    int seedMaxY = std::min(maxSolidY + 2, CHUNK_HEIGHT);
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < seedMaxY; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (skyLight[slIdx(x, y, z)] <= 1) continue;
                // Check if any neighbor is darker (worth seeding from)
                bool hasEdge = (x == 0 || x == CHUNK_SIZE - 1 || z == 0 || z == CHUNK_SIZE - 1);
                if (!hasEdge) {
                    uint8_t myLight = skyLight[slIdx(x, y, z)];
                    hasEdge = (y > 0 && skyLight[slIdx(x, y - 1, z)] < myLight - 1) ||
                              (skyLight[slIdx(x - 1 < 0 ? 0 : x - 1, y, z)] < myLight - 1) ||
                              (skyLight[slIdx(x + 1 >= CHUNK_SIZE ? CHUNK_SIZE - 1 : x + 1, y, z)] < myLight - 1) ||
                              (skyLight[slIdx(x, y, z - 1 < 0 ? 0 : z - 1)] < myLight - 1) ||
                              (skyLight[slIdx(x, y, z + 1 >= CHUNK_SIZE ? CHUNK_SIZE - 1 : z + 1)] < myLight - 1);
                }
                if (hasEdge) queue.push_back(packCoord(x, y, z));
            }

    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    size_t head = 0;
    while (head < queue.size()) {
        int32_t packed = queue[head++];
        int x = packed >> 20, y = (packed >> 8) & 0xFFF, z = packed & 0xFF;
        uint8_t light = skyLight[slIdx(x, y, z)];
        if (light <= 1) continue;

        for (auto& d : DIRS) {
            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= scanH || nz < 0 || nz >= CHUNK_SIZE) continue;

            if (blockInfo[biIdx(nx, ny, nz)] & 1) continue; // opaque

            uint8_t newLight = light - 1;
            if (skyLight[slIdx(nx, ny, nz)] >= newLight) continue;
            skyLight[slIdx(nx, ny, nz)] = newLight;
            queue.push_back(packCoord(nx, ny, nz));
        }
    }
}

void Chunk::computeSkyLight() {
    computeSkyLightData(blocks.get(), skyLight.get(), maxSolidY);
}

uint8_t Chunk::getSkyLight(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 15;
    return skyLight[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z];
}

Cube* Chunk::getBlock(int i, int j, int k) {
    if (i < 0 || i >= CHUNK_SIZE || j < 0 || j >= CHUNK_HEIGHT || k < 0 || k >= CHUNK_SIZE) return nullptr;
    return &blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
}

// Returns the block at local (i,j,k), crossing into a neighbor chunk if needed.
// Returns nullptr when out of vertical bounds or when the neighbor chunk isn't loaded.
static Cube* getBlockCross(Chunk* self, int i, int j, int k, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg,
                           Chunk* nz_pos) {
    if (i < 0) return nx_neg ? nx_neg->getBlock(CHUNK_SIZE - 1, j, k) : nullptr;
    if (i >= CHUNK_SIZE) return nx_pos ? nx_pos->getBlock(0, j, k) : nullptr;
    if (k < 0) return nz_neg ? nz_neg->getBlock(i, j, CHUNK_SIZE - 1) : nullptr;
    if (k >= CHUNK_SIZE) return nz_pos ? nz_pos->getBlock(i, j, 0) : nullptr;
    return self->getBlock(i, j, k);
}

// Check if block at (i,j,k) is opaque (solid, not air/water). Uses cross-chunk lookup.
static bool isOpaqueCross(Chunk* self, int i, int j, int k, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg,
                          Chunk* nz_pos) {
    Cube* b = getBlockCross(self, i, j, k, nx_neg, nx_pos, nz_neg, nz_pos);
    if (!b) return false;
    block_type t = b->getType();
    return t != AIR && t != WATER;
}

void Chunk::buildMeshData(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    double _buildStart = glfwGetTime();

    // Precompute opacity cache with 1-block padding for fast AO lookups.
    int opaqueH = std::min(maxSolidY + 2, (int)CHUNK_HEIGHT);
    constexpr int OX = CHUNK_SIZE + 2, OZ = CHUNK_SIZE + 2;
    std::vector<uint8_t> opaq(static_cast<size_t>(OX) * opaqueH * OZ, 0);
    auto oIdx = [&](int x, int y, int z) -> int { return (x + 1) * opaqueH * OZ + y * OZ + (z + 1); };

    // Fill interior
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < opaqueH; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                block_type t = blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].getType();
                opaq[oIdx(x, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }
    // Fill borders from neighbors
    for (int y = 0; y < opaqueH; y++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            if (nx_neg) {
                block_type t = nx_neg->getBlock(CHUNK_SIZE - 1, y, z)->getType();
                opaq[oIdx(-1, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }
            if (nx_pos) {
                block_type t = nx_pos->getBlock(0, y, z)->getType();
                opaq[oIdx(CHUNK_SIZE, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }
        }
    for (int y = 0; y < opaqueH; y++)
        for (int x = 0; x < CHUNK_SIZE; x++) {
            if (nz_neg) {
                block_type t = nz_neg->getBlock(x, y, CHUNK_SIZE - 1)->getType();
                opaq[oIdx(x, y, -1)] = (t != AIR && t != WATER) ? 1 : 0;
            }
            if (nz_pos) {
                block_type t = nz_pos->getBlock(x, y, 0)->getType();
                opaq[oIdx(x, y, CHUNK_SIZE)] = (t != AIR && t != WATER) ? 1 : 0;
            }
        }

    // Greedy meshing: merge coplanar same-type adjacent faces into larger quads.
    // Vertex layout: pos(3) + texcoord(2) + normal(3) + texLayer(1) + ao(1) = 10 floats/vertex.

    struct FaceDef {
        int d, u, v;                // axis indices for normal, first tangent, second tangent
        int d_sign, u_sign, v_sign; // direction signs (±1) for correct winding order
    };
    static constexpr FaceDef FACE_DEFS[6] = {
        {2, 0, 1, 1, 1, 1},   // Front +Z
        {2, 0, 1, -1, -1, 1}, // Back  -Z
        {0, 2, 1, -1, 1, 1},  // Left  -X
        {0, 2, 1, 1, -1, 1},  // Right +X
        {1, 0, 2, 1, 1, -1},  // Top   +Y
        {1, 0, 2, -1, 1, 1},  // Bottom -Y
    };

    // Per-axis dimensions: x=CHUNK_SIZE, y=CHUNK_HEIGHT, z=CHUNK_SIZE
    static constexpr int DIM[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
    static constexpr int MAX_DIM = CHUNK_HEIGHT > CHUNK_SIZE ? CHUNK_HEIGHT : CHUNK_SIZE;

    constexpr size_t MAX_FACES = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * 4;
    constexpr int FLOATS_PER_VERT = 11;
    std::vector<float> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * FLOATS_PER_VERT);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4 * FLOATS_PER_VERT);
    waterIdx.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

    // Pre-cache sky light values in a flat array for fast access (avoids getSkyLight bounds checks)
    uint8_t* slPtr = skyLight.get();
    auto slDirect = [slPtr](int x, int y, int z) -> float {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE)
            return 0.15f + 0.85f;
        uint8_t sl = slPtr[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z];
        return 0.15f + 0.85f * (sl / 15.0f);
    };

    int mask[MAX_DIM][MAX_DIM];
    const float worldOff[3] = {(float)(chunkX * CHUNK_SIZE), 0.0f, (float)(chunkY * CHUNK_SIZE)};

    // Effective dimensions capped by maxSolidY+1 (skip pure-air region above)
    const int effDIM[3] = {CHUNK_SIZE, maxSolidY + 2, CHUNK_SIZE}; // +2: need one slice above for top faces

    for (int f = 0; f < 6; f++) {
        const FaceDef& fd = FACE_DEFS[f];
        const glm::vec3& norm = FACE_NORMALS[f];
        const int d_dim = std::min(DIM[fd.d], effDIM[fd.d]);
        const int u_dim = std::min(DIM[fd.u], effDIM[fd.u]);
        const int v_dim = std::min(DIM[fd.v], effDIM[fd.v]);

        for (int d = 0; d < d_dim; d++) {

            // 1. Build mask for this face direction and slice
            bool anyFace = false;
            for (int u = 0; u < u_dim; u++) {
                for (int v = 0; v < v_dim; v++) {
                    int c[3];
                    c[fd.d] = d;
                    c[fd.u] = u;
                    c[fd.v] = v;
                    block_type bt = getBlock(c[0], c[1], c[2])->getType();
                    // Skip air; skip liquid sides (only render top face, f==4)
                    if (bt == AIR || (hasFlag(bt, BF_LIQUID) && f != 4)) {
                        mask[u][v] = -1;
                        continue;
                    }

                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    Cube* nb = getBlockCross(this, nc[0], nc[1], nc[2], nx_neg, nx_pos, nz_neg, nz_pos);
                    block_type nbType = nb ? nb->getType() : STONE; // missing neighbor = solid, suppress face
                    int val = (nbType == AIR || (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID))) ? (int)bt : -1;
                    mask[u][v] = val;
                    if (val != -1) anyFace = true;
                }
            }

            if (!anyFace) continue; // entire slice is air, skip greedy sweep

            // 2. Sweep mask and emit quads
            for (int u = 0; u < u_dim; u++) {
                for (int v = 0; v < v_dim;) {
                    int bt = mask[u][v];
                    if (bt == -1) {
                        v++;
                        continue;
                    }

                    int h = 1, w = 1;
                    if (g_greedyMeshing) {
                        while (v + h < v_dim && mask[u][v + h] == bt) h++;
                        w = 1;
                        while (u + w < u_dim) {
                            bool ok = true;
                            for (int dv = 0; dv < h && ok; dv++) ok = (mask[u + w][v + dv] == bt);
                            if (!ok) break;
                            w++;
                        }
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
                    vp[0][fd.d] = d_val;
                    vp[0][fd.u] = u0;
                    vp[0][fd.v] = v0;
                    vp[1][fd.d] = d_val;
                    vp[1][fd.u] = u0;
                    vp[1][fd.v] = v1;
                    vp[2][fd.d] = d_val;
                    vp[2][fd.u] = u1;
                    vp[2][fd.v] = v1;
                    vp[3][fd.d] = d_val;
                    vp[3][fd.u] = u1;
                    vp[3][fd.v] = v0;

                    const float uvs[4][2] = {{0.f, 0.f}, {0.f, (float)h}, {(float)w, (float)h}, {(float)w, 0.f}};

                    // Per-vertex AO: check 3 corner neighbors per vertex
                    // For each vertex, find the block at that corner and the
                    // two side + one diagonal neighbor in the face's normal direction.
                    static const float AO_CURVE[4] = {0.55f, 0.7f, 0.85f, 1.0f};
                    float skyLightVals[4];
                    int bu[4], bv[4], cu[4], cv[4];
                    bu[0] = bu[1] = (fd.u_sign > 0) ? u : u + w - 1;
                    bu[2] = bu[3] = (fd.u_sign > 0) ? u + w - 1 : u;
                    bv[0] = bv[3] = (fd.v_sign > 0) ? v : v + h - 1;
                    bv[1] = bv[2] = (fd.v_sign > 0) ? v + h - 1 : v;
                    cu[0] = cu[1] = -fd.u_sign;
                    cu[2] = cu[3] = fd.u_sign;
                    cv[0] = cv[3] = -fd.v_sign;
                    cv[1] = cv[2] = fd.v_sign;

                    float ao[4];
                    for (int vi = 0; vi < 4; vi++) {
                        int bc[3];
                        bc[fd.d] = d;
                        bc[fd.u] = bu[vi];
                        bc[fd.v] = bv[vi];
                        int n[3] = {0, 0, 0};
                        n[fd.d] = fd.d_sign;
                        int su[3] = {0, 0, 0};
                        su[fd.u] = cu[vi];
                        int sv[3] = {0, 0, 0};
                        sv[fd.v] = cv[vi];

                        // Fast AO lookup using precomputed opacity cache
                        int px = bc[0] + n[0], py = bc[1] + n[1], pz = bc[2] + n[2];
                        int s1 = (py + su[1] >= 0 && py + su[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0], py + su[1], pz + su[2])]
                                     : 0;
                        int s2 = (py + sv[1] >= 0 && py + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + sv[0], py + sv[1], pz + sv[2])]
                                     : 0;
                        int cr = (py + su[1] + sv[1] >= 0 && py + su[1] + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0] + sv[0], py + su[1] + sv[1], pz + su[2] + sv[2])]
                                     : 0;
                        int aoVal = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);

                        ao[vi] = AO_CURVE[aoVal];

                        skyLightVals[vi] = slDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                    }

                    bool isWater = (bt == (int)WATER);
                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx = isWater ? waterIdx : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    // Batch-write 4 vertices (44 floats) at once
                    size_t off = verts.size();
                    verts.resize(off + 4 * FLOATS_PER_VERT);
                    float* dst = &verts[off];
                    for (int vi = 0; vi < 4; vi++) {
                        dst[0] = vp[vi][0] + worldOff[0];
                        dst[1] = vp[vi][1] + worldOff[1];
                        dst[2] = vp[vi][2] + worldOff[2];
                        dst[3] = uvs[vi][0];
                        dst[4] = uvs[vi][1];
                        dst[5] = norm.x;
                        dst[6] = norm.y;
                        dst[7] = norm.z;
                        dst[8] = layer;
                        dst[9] = ao[vi];
                        dst[10] = skyLightVals[vi];
                        dst += FLOATS_PER_VERT;
                    }
                    // Flip quad diagonal when AO is asymmetric to avoid interpolation artifacts
                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        // Default diagonal: 0-1-2, 2-3-0
                        idx.push_back(base);
                        idx.push_back(base + 1);
                        idx.push_back(base + 2);
                        idx.push_back(base + 2);
                        idx.push_back(base + 3);
                        idx.push_back(base);
                    } else {
                        // Flipped diagonal: 1-2-3, 3-0-1
                        idx.push_back(base + 1);
                        idx.push_back(base + 2);
                        idx.push_back(base + 3);
                        idx.push_back(base + 3);
                        idx.push_back(base);
                        idx.push_back(base + 1);
                    }
                    base += 4;

                    if (g_greedyMeshing) {
                        for (int du = 0; du < w; du++)
                            for (int dv = 0; dv < h; dv++) mask[u + du][v + dv] = -1;
                    }
                    v += h;
                }
            }
        }
    }

    // Combine into one VBO/EBO (opaque first, then water) — append in-place
    opaqueVerts.insert(opaqueVerts.end(), waterVerts.begin(), waterVerts.end());
    for (auto idx_val : waterIdx) opaqueIdx.push_back(idx_val + opaqueBase);

    pendingMesh.verts = std::move(opaqueVerts);
    pendingMesh.waterIdx.resize(waterIdx.size());
    pendingMesh.opaqueIdx = std::move(opaqueIdx);
    pendingMesh.ready = true;

    g_frame.meshBuildMs += (glfwGetTime() - _buildStart) * 1000.0;
    g_frame.meshBuilds++;
}

void Chunk::buildMesh(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    buildMeshData(nx_neg, nx_pos, nz_neg, nz_pos);
}

Chunk::NeighborBorders Chunk::snapshotBorders(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    NeighborBorders nb;
    if (nx_neg) {
        nb.xNeg.valid = true;
        for (int z = 0; z < CHUNK_SIZE; z++)
            for (int y = 0; y < CHUNK_HEIGHT; y++)
                nb.xNeg.types[z][y] = nx_neg->getBlock(CHUNK_SIZE - 1, y, z)->getType();
    }
    if (nx_pos) {
        nb.xPos.valid = true;
        for (int z = 0; z < CHUNK_SIZE; z++)
            for (int y = 0; y < CHUNK_HEIGHT; y++)
                nb.xPos.types[z][y] = nx_pos->getBlock(0, y, z)->getType();
    }
    if (nz_neg) {
        nb.zNeg.valid = true;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int y = 0; y < CHUNK_HEIGHT; y++)
                nb.zNeg.types[x][y] = nz_neg->getBlock(x, y, CHUNK_SIZE - 1)->getType();
    }
    if (nz_pos) {
        nb.zPos.valid = true;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int y = 0; y < CHUNK_HEIGHT; y++)
                nb.zPos.types[x][y] = nz_pos->getBlock(x, y, 0)->getType();
    }
    return nb;
}

// Free function: build mesh from raw data — fully thread-safe, no GL calls
Chunk::MeshData buildMeshFromData(Cube* blocks, uint8_t* skyLight,
                                  int maxSolidY, int chunkX, int chunkZ,
                                  const Chunk::NeighborBorders& nb) {

    int opaqueH = std::min(maxSolidY + 2, (int)CHUNK_HEIGHT);
    constexpr int OX = CHUNK_SIZE + 2, OZ = CHUNK_SIZE + 2;
    std::vector<uint8_t> opaq(static_cast<size_t>(OX) * opaqueH * OZ, 0);
    auto oIdx = [&](int x, int y, int z) -> int { return (x + 1) * opaqueH * OZ + y * OZ + (z + 1); };

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < opaqueH; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                block_type t = blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].getType();
                opaq[oIdx(x, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }

    for (int y = 0; y < opaqueH; y++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            if (nb.xNeg.valid) {
                block_type t = nb.xNeg.types[z][y];
                opaq[oIdx(-1, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }
            if (nb.xPos.valid) {
                block_type t = nb.xPos.types[z][y];
                opaq[oIdx(CHUNK_SIZE, y, z)] = (t != AIR && t != WATER) ? 1 : 0;
            }
        }
    for (int y = 0; y < opaqueH; y++)
        for (int x = 0; x < CHUNK_SIZE; x++) {
            if (nb.zNeg.valid) {
                block_type t = nb.zNeg.types[x][y];
                opaq[oIdx(x, y, -1)] = (t != AIR && t != WATER) ? 1 : 0;
            }
            if (nb.zPos.valid) {
                block_type t = nb.zPos.types[x][y];
                opaq[oIdx(x, y, CHUNK_SIZE)] = (t != AIR && t != WATER) ? 1 : 0;
            }
        }

    // Helper: get block type from borders (replaces getBlockCross)
    // Missing neighbors treated as solid — suppresses border faces until neighbor loads
    auto getTypeCross = [&](int i, int j, int k) -> block_type {
        if (j < 0 || j >= CHUNK_HEIGHT) return AIR;
        if (i >= 0 && i < CHUNK_SIZE && k >= 0 && k < CHUNK_SIZE)
            return blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k].getType();
        if (i < 0) return nb.xNeg.valid ? nb.xNeg.types[k][j] : STONE;
        if (i >= CHUNK_SIZE) return nb.xPos.valid ? nb.xPos.types[k][j] : STONE;
        if (k < 0) return nb.zNeg.valid ? nb.zNeg.types[i][j] : STONE;
        if (k >= CHUNK_SIZE) return nb.zPos.valid ? nb.zPos.types[i][j] : STONE;
        return STONE;
    };

    // --- Same mesh construction as buildMeshData but using getTypeCross ---
    constexpr int FLOATS_PER_VERT = 11;
    struct FaceDef {
        int d, u, v;
        int d_sign, u_sign, v_sign;
    };
    static constexpr FaceDef FACE_DEFS[6] = {
        {2, 0, 1, 1, 1, 1},   {2, 0, 1, -1, -1, 1},
        {0, 2, 1, -1, 1, 1},  {0, 2, 1, 1, -1, 1},
        {1, 0, 2, 1, 1, -1},  {1, 0, 2, -1, 1, 1},
    };
    static constexpr int DIM[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
    static constexpr int MAX_DIM = CHUNK_HEIGHT > CHUNK_SIZE ? CHUNK_HEIGHT : CHUNK_SIZE;

    constexpr size_t MAX_FACES = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * 4;
    std::vector<float> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * FLOATS_PER_VERT);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4 * FLOATS_PER_VERT);
    waterIdx.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

    auto slDirect = [skyLight](int x, int y, int z) -> float {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE)
            return 0.15f + 0.85f;
        uint8_t sl = skyLight[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z];
        return 0.15f + 0.85f * (sl / 15.0f);
    };

    int mask[MAX_DIM][MAX_DIM];
    const float worldOff[3] = {(float)(chunkX * CHUNK_SIZE), 0.0f, (float)(chunkZ * CHUNK_SIZE)};
    const int effDIM[3] = {CHUNK_SIZE, maxSolidY + 2, CHUNK_SIZE};

    for (int f = 0; f < 6; f++) {
        const FaceDef& fd = FACE_DEFS[f];
        const glm::vec3& norm = FACE_NORMALS[f];
        const int d_dim = std::min(DIM[fd.d], effDIM[fd.d]);
        const int u_dim = std::min(DIM[fd.u], effDIM[fd.u]);
        const int v_dim = std::min(DIM[fd.v], effDIM[fd.v]);

        for (int d = 0; d < d_dim; d++) {
            bool anyFace = false;
            for (int u = 0; u < u_dim; u++) {
                for (int v = 0; v < v_dim; v++) {
                    int c[3]; c[fd.d] = d; c[fd.u] = u; c[fd.v] = v;
                    block_type bt = blocks[c[0] * CHUNK_HEIGHT * CHUNK_SIZE + c[1] * CHUNK_SIZE + c[2]].getType();
                    if (bt == AIR || (hasFlag(bt, BF_LIQUID) && f != 4)) {
                        mask[u][v] = -1; continue;
                    }
                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    block_type nbType = getTypeCross(nc[0], nc[1], nc[2]);
                    int val = (nbType == AIR || (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID))) ? (int)bt : -1;
                    mask[u][v] = val;
                    if (val != -1) anyFace = true;
                }
            }
            if (!anyFace) continue;

            for (int u = 0; u < u_dim; u++) {
                for (int v = 0; v < v_dim;) {
                    int bt = mask[u][v];
                    if (bt == -1) { v++; continue; }

                    int h = 1, w = 1;
                    if (g_greedyMeshing) {
                        while (v + h < v_dim && mask[u][v + h] == bt) h++;
                        w = 1;
                        while (u + w < u_dim) {
                            bool ok = true;
                            for (int dv = 0; dv < h && ok; dv++) ok = (mask[u + w][v + dv] == bt);
                            if (!ok) break; w++;
                        }
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
                    vp[0][fd.d] = d_val; vp[0][fd.u] = u0; vp[0][fd.v] = v0;
                    vp[1][fd.d] = d_val; vp[1][fd.u] = u0; vp[1][fd.v] = v1;
                    vp[2][fd.d] = d_val; vp[2][fd.u] = u1; vp[2][fd.v] = v1;
                    vp[3][fd.d] = d_val; vp[3][fd.u] = u1; vp[3][fd.v] = v0;

                    const float uvs[4][2] = {{0.f, 0.f}, {0.f, (float)h}, {(float)w, (float)h}, {(float)w, 0.f}};

                    static const float AO_CURVE[4] = {0.55f, 0.7f, 0.85f, 1.0f};
                    float skyLightVals[4];
                    int bu[4], bv[4], cu[4], cv[4];
                    bu[0] = bu[1] = (fd.u_sign > 0) ? u : u + w - 1;
                    bu[2] = bu[3] = (fd.u_sign > 0) ? u + w - 1 : u;
                    bv[0] = bv[3] = (fd.v_sign > 0) ? v : v + h - 1;
                    bv[1] = bv[2] = (fd.v_sign > 0) ? v + h - 1 : v;
                    cu[0] = cu[1] = -fd.u_sign; cu[2] = cu[3] = fd.u_sign;
                    cv[0] = cv[3] = -fd.v_sign; cv[1] = cv[2] = fd.v_sign;

                    float ao[4];
                    for (int vi = 0; vi < 4; vi++) {
                        int bc[3]; bc[fd.d] = d; bc[fd.u] = bu[vi]; bc[fd.v] = bv[vi];
                        int n[3] = {0, 0, 0}; n[fd.d] = fd.d_sign;
                        int su[3] = {0, 0, 0}; su[fd.u] = cu[vi];
                        int sv[3] = {0, 0, 0}; sv[fd.v] = cv[vi];

                        int px = bc[0] + n[0], py = bc[1] + n[1], pz = bc[2] + n[2];
                        int s1 = (py + su[1] >= 0 && py + su[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0], py + su[1], pz + su[2])] : 0;
                        int s2 = (py + sv[1] >= 0 && py + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + sv[0], py + sv[1], pz + sv[2])] : 0;
                        int cr = (py + su[1] + sv[1] >= 0 && py + su[1] + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0] + sv[0], py + su[1] + sv[1], pz + su[2] + sv[2])] : 0;
                        int aoVal = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
                        ao[vi] = AO_CURVE[aoVal];
                        skyLightVals[vi] = slDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                    }

                    bool isWater = (bt == (int)WATER);
                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx = isWater ? waterIdx : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    size_t off = verts.size();
                    verts.resize(off + 4 * FLOATS_PER_VERT);
                    float* dst = &verts[off];
                    for (int vi = 0; vi < 4; vi++) {
                        dst[0] = vp[vi][0] + worldOff[0]; dst[1] = vp[vi][1] + worldOff[1];
                        dst[2] = vp[vi][2] + worldOff[2]; dst[3] = uvs[vi][0]; dst[4] = uvs[vi][1];
                        dst[5] = norm.x; dst[6] = norm.y; dst[7] = norm.z;
                        dst[8] = layer; dst[9] = ao[vi]; dst[10] = skyLightVals[vi];
                        dst += FLOATS_PER_VERT;
                    }

                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
                        idx.push_back(base + 2); idx.push_back(base + 3); idx.push_back(base);
                    } else {
                        idx.push_back(base + 1); idx.push_back(base + 2); idx.push_back(base + 3);
                        idx.push_back(base + 3); idx.push_back(base); idx.push_back(base + 1);
                    }
                    base += 4;

                    if (g_greedyMeshing) {
                        for (int du = 0; du < w; du++)
                            for (int dv = 0; dv < h; dv++) mask[u + du][v + dv] = -1;
                    }
                    v += h;
                }
            }
        }
    }

    opaqueVerts.insert(opaqueVerts.end(), waterVerts.begin(), waterVerts.end());
    for (auto idx_val : waterIdx) opaqueIdx.push_back(idx_val + opaqueBase);

    Chunk::MeshData result;
    result.verts = std::move(opaqueVerts);
    result.waterIdx.resize(waterIdx.size());
    result.opaqueIdx = std::move(opaqueIdx);
    result.ready = true;
    return result;
}

void Chunk::buildMeshDataAsync(const NeighborBorders& borders) {
    double _buildStart = glfwGetTime();
    pendingMesh = buildMeshFromData(blocks.get(), skyLight.get(), maxSolidY, chunkX, chunkY, borders);
    g_frame.meshBuildMs += (glfwGetTime() - _buildStart) * 1000.0;
    g_frame.meshBuilds++;
}

void Chunk::uploadMesh() {
    if (!pendingMesh.ready) return;

    if (chunkVAO == 0) {
        glGenVertexArrays(1, &chunkVAO);
        glGenBuffers(1, &chunkVBO);
        glGenBuffers(1, &chunkEBO);
    }

    glBindVertexArray(chunkVAO);

    glBindBuffer(GL_ARRAY_BUFFER, chunkVBO);
    glBufferData(GL_ARRAY_BUFFER, pendingMesh.verts.size() * sizeof(float), pendingMesh.verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunkEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, pendingMesh.opaqueIdx.size() * sizeof(unsigned int),
                 pendingMesh.opaqueIdx.data(), GL_DYNAMIC_DRAW);

    constexpr int STRIDE = 11 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, STRIDE, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(10 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0);

    // Track counts before clearing
    int totalOpaqueIdx = (int)pendingMesh.opaqueIdx.size() - (int)pendingMesh.waterIdx.size();
    opaqueIndexCount = totalOpaqueIdx;
    waterIndexCount = (int)pendingMesh.waterIdx.size();
    waterIndexOffset = totalOpaqueIdx * sizeof(unsigned int);
    meshDirty = false;

    // Free CPU-side data
    pendingMesh.verts.clear();
    pendingMesh.verts.shrink_to_fit();
    pendingMesh.opaqueIdx.clear();
    pendingMesh.opaqueIdx.shrink_to_fit();
    pendingMesh.waterIdx.clear();
    pendingMesh.waterIdx.shrink_to_fit();
    pendingMesh.ready = false;
}

std::vector<Cube*> Chunk::render(const Shader& /*shaderProgram*/, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg,
                                 Chunk* nz_pos) {
    if (pendingMesh.ready) {
        uploadMesh();
    } else if (meshDirty && !meshBuildInFlight && g_frame.meshBuildBudget > 0) {
        // Fallback: build on main thread if not queued for async
        buildMesh(nx_neg, nx_pos, nz_neg, nz_pos);
        uploadMesh();
        g_frame.meshBuildBudget--;
    }

    if (opaqueIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, opaqueIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        g_frame.opaqueTriangles += opaqueIndexCount / 3;
        g_frame.vertexCount += opaqueIndexCount / 6 * 4; // 4 verts per 6 indices (quad)
        g_frame.opaqueDrawCalls++;
    }
    return {};
}

void Chunk::renderWater(const Shader& /*shaderProgram*/, Chunk* /*nx_neg*/, Chunk* /*nx_pos*/, Chunk* /*nz_neg*/,
                        Chunk* /*nz_pos*/) {
    if (waterIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT, (void*)waterIndexOffset);
        glBindVertexArray(0);
        g_frame.waterTriangles += waterIndexCount / 3;
        g_frame.waterDrawCalls++;
    }
}

// Local sky light update: BFS from a single position, propagates to nearby air blocks
static void updateSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    auto idx = [](int bx, int by, int bz) -> size_t {
        return static_cast<size_t>(bx) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(by) * CHUNK_SIZE + bz;
    };
    static const int DIRS[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    // Determine light at this position from neighbors
    uint8_t maxLight = 0;
    for (auto& d : DIRS) {
        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_HEIGHT && nz >= 0 && nz < CHUNK_SIZE) {
            uint8_t nl = skyLight[idx(nx, ny, nz)];
            if (nl > maxLight) maxLight = nl;
        }
    }
    if (y + 1 >= CHUNK_HEIGHT) maxLight = 15;

    // Inherit full sky light if directly below a sky-lit column
    uint8_t newLight = (y + 1 < CHUNK_HEIGHT && skyLight[idx(x, y + 1, z)] == 15) ? 15
                       : (maxLight > 0 ? maxLight - 1 : 0);
    skyLight[idx(x, y, z)] = newLight;

    // BFS outward
    struct Node { int x, y, z; };
    std::vector<Node> queue;
    queue.reserve(256);
    queue.push_back({x, y, z});

    size_t head = 0;
    while (head < queue.size()) {
        auto [cx, cy, cz] = queue[head++];
        uint8_t light = skyLight[idx(cx, cy, cz)];
        if (light <= 1) continue;

        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[idx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;

            uint8_t propagated = light - 1;
            if (skyLight[idx(nx, ny, nz)] >= propagated) continue;
            skyLight[idx(nx, ny, nz)] = propagated;
            queue.push_back({nx, ny, nz});
        }
    }
}

void Chunk::destroyBlock(int x, int y, int z) {
    blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].setType(AIR);
    updateSkyLightLocal(blocks.get(), skyLight.get(), x, y, z);
    meshDirty = true;
}

int Chunk::getLocalHeight(int x, int y) {
    return heights[x][y];
}

int Chunk::getGlobalHeight(int x, int y) {
    int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    int lz = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    return heights[lx][lz];
}

void Chunk::destroy() {
    if (chunkVAO) {
        glDeleteVertexArrays(1, &chunkVAO);
        chunkVAO = 0;
    }
    if (chunkVBO) {
        glDeleteBuffers(1, &chunkVBO);
        chunkVBO = 0;
    }
    if (chunkEBO) {
        glDeleteBuffers(1, &chunkEBO);
        chunkEBO = 0;
    }
    blocks.reset();
    skyLight.reset();
}

Chunk::~Chunk() {
    if (blocks) destroy();
}
