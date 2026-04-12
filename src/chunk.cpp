/**
 * @file chunk.cpp
 */

#include "chunk.h"
#include "light_data.h"
#include "texture_array.h"
#include "profiler.h"
#include "gl_header.h"
#include <random>
#include <algorithm>
#include <chrono>

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

#pragma pack(push, 1)
struct PackedVertex {
    int16_t px, py, pz;
    uint8_t u, v;
    uint8_t normalIdx, texLayer, ao, packedLight;
};
#pragma pack(pop)
static_assert(sizeof(PackedVertex) == 12);
static constexpr int BYTES_PER_VERT = 12;

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

#include "tracy_shim.h"

// Per-cell water surface height. Falling water renders full-height; otherwise
// the height is derived from the flow level (level 0 ≈ 0.89, level 7 ≈ 0.11).
// Returns -1 if the cell is out of bounds or not water. Water levels live in
// a parallel uint8_t[] that tracks the `blocks` layout 1:1.
static float waterCellHeight(const Cube* blocks, const uint8_t* waterLevels,
                             int bx, int by, int bz) {
    if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE || by < 0 || by >= CHUNK_HEIGHT)
        return -1.0f;
    size_t i = static_cast<size_t>(bx) * CHUNK_HEIGHT * CHUNK_SIZE + by * CHUNK_SIZE + bz;
    if (blocks[i].getType() != WATER) return -1.0f;
    uint8_t raw = waterLevels ? waterLevels[i] : 0;
    if (waterIsFalling(raw)) return 8.0f / 9.0f;
    return (8.0f - waterFlowLevel(raw)) / 9.0f;
}

// Compute the four top-face corner heights of a water block by averaging
// this cell with the up-to-three neighbors sharing each corner. Corner
// offsets are flipped based on the face axis signs so they match the
// quad's vertex order (SW, NW, NE, SE when u_sign/v_sign > 0).
static void computeWaterTopCorners(const Cube* blocks, const uint8_t* waterLevels,
                                   int bx, int by, int bz,
                                   int uSign, int vSign, float out[4]) {
    int cdx[4] = {-1, -1, 1, 1};
    int cdz[4] = {-1, 1, 1, -1};
    if (uSign < 0) {
        std::swap(cdx[0], cdx[3]);
        std::swap(cdx[1], cdx[2]);
        std::swap(cdz[0], cdz[3]);
        std::swap(cdz[1], cdz[2]);
    }
    if (vSign < 0) {
        std::swap(cdx[0], cdx[1]);
        std::swap(cdx[2], cdx[3]);
        std::swap(cdz[0], cdz[1]);
        std::swap(cdz[2], cdz[3]);
    }
    float cH = waterCellHeight(blocks, waterLevels, bx, by, bz);
    for (int ci = 0; ci < 4; ci++) {
        float sum = cH;
        int cnt = 1;
        float h1 = waterCellHeight(blocks, waterLevels, bx + cdx[ci], by, bz);
        float h2 = waterCellHeight(blocks, waterLevels, bx, by, bz + cdz[ci]);
        float h3 = waterCellHeight(blocks, waterLevels, bx + cdx[ci], by, bz + cdz[ci]);
        if (h1 >= 0) { sum += h1; cnt++; }
        if (h2 >= 0) { sum += h2; cnt++; }
        if (h3 >= 0) { sum += h3; cnt++; }
        out[ci] = (float)by + sum / cnt;
    }
}

// Helper for block access in a flat Cube[] buffer (same layout as old Chunk::getBlock)
static Cube* getBlockFromFlat(Cube* blocks, int i, int j, int k) {
    if (i < 0 || i >= CHUNK_SIZE || j < 0 || j >= CHUNK_HEIGHT || k < 0 || k >= CHUNK_SIZE) return nullptr;
    return &blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
}

// Compress a flat Cube[] buffer into ChunkData sections
static void compressIntoSections(std::unique_ptr<ChunkSection> sections[], const Cube* flat) {
    for (int s = 0; s < NUM_SECTIONS; s++) {
        // Temp buffer for this section's 4096 blocks (section layout: x*256+y*16+z)
        Cube sectionBuf[ChunkSection::VOLUME];
        bool allAir = true;
        int baseY = s * 16;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int ly = 0; ly < 16; ly++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int y = baseY + ly;
                    block_type bt = flat[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].getType();
                    sectionBuf[x * 16 * 16 + ly * 16 + z].setType(bt);
                    if (bt != AIR) allAir = false;
                }
        if (allAir) {
            sections[s].reset();
        } else {
            sections[s] = std::make_unique<ChunkSection>();
            sections[s]->compress(sectionBuf);
        }
    }
}

ChunkData generateChunkData(int chunkX, int chunkZ, TerrainGenerator& terrain) {
    ChunkData d;
    // Generate terrain into a temporary flat buffer, then compress into sections
    auto flatBlocks = std::shared_ptr<Cube[]>(new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]);
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
                Cube* block = &flatBlocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
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
                Cube* block = getBlockFromFlat(flatBlocks.get(),i, j, k);
                block_type bt = block->getType();
                if (bt != DIRT && bt != GRASS && bt != STONE && bt != GRAVEL) continue;

                if (j < WATER_LEVEL) {
                    Cube* above = getBlockFromFlat(flatBlocks.get(),i, j + 1, k);
                    if (above && above->getType() == WATER) {
                        block->setType(shoreBlock);
                        continue;
                    }
                }
                static const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (auto& dir : dirs) {
                    Cube* nb = getBlockFromFlat(flatBlocks.get(),i + dir[0], j + dir[1], k + dir[2]);
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

        Cube* surfaceBlock = getBlockFromFlat(flatBlocks.get(),tx, surface, tz);
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
            Cube* b = getBlockFromFlat(flatBlocks.get(),tx, y, tz);
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
                    Cube* b = getBlockFromFlat(flatBlocks.get(),bx, by, bz);
                    if (b && b->getType() == AIR) b->setType(LEAVES);
                }
            }
        }

        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                if (abs(dx) == 1 && abs(dz) == 1) continue;
                int bx = tx + dx, bz = tz + dz;
                if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
                Cube* crown = getBlockFromFlat(flatBlocks.get(),bx, canopyBase + layers, bz);
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

        Cube* surfBlock = getBlockFromFlat(flatBlocks.get(),cx, surface, cz);
        if (!surfBlock || surfBlock->getType() != SAND) continue;

        for (int y = surface + 1; y <= surface + cactusH; y++) {
            Cube* b = getBlockFromFlat(flatBlocks.get(),cx, y, cz);
            if (b && b->getType() == AIR) b->setType(CACTUS);
        }
    }

    // Compute maxSolidY
    d.maxSolidY = 0;
    for (int j = CHUNK_HEIGHT - 1; j >= 0; j--) {
        bool found = false;
        for (int i = 0; i < CHUNK_SIZE && !found; i++)
            for (int k = 0; k < CHUNK_SIZE && !found; k++)
                if (getBlockFromFlat(flatBlocks.get(), i, j, k)->getType() != AIR) {
                    d.maxSolidY = j;
                    found = true;
                }
        if (found) break;
    }

    computeSkyLightData(flatBlocks.get(), d.skyLight.get(), d.maxSolidY);

    // Compress flat buffer into palette-based sections
    compressIntoSections(d.sections, flatBlocks.get());

    return d;
}

// Existing constructor now delegates to generateChunkData
Chunk::Chunk(int chunkX, int chunkY, TerrainGenerator& terrain) : Chunk(generateChunkData(chunkX, chunkY, terrain)) {}

// Construct from pre-generated data (no computation, main thread only)
Chunk::Chunk(ChunkData&& data) {
    for (int i = 0; i < NUM_SECTIONS; i++) sections[i] = std::move(data.sections[i]);
    skyLight = std::move(data.skyLight);
    waterLevels = std::move(data.waterLevels);
    this->chunkX = data.chunkX;
    this->chunkY = data.chunkZ;
    maxSolidY = data.maxSolidY;
    std::memcpy(heights, data.heights, sizeof(heights));
    std::memcpy(biomes, data.biomes, sizeof(biomes));
    sectionDirty = 0xFF;
}

static bool isBlockOpaque(block_type t) {
    return hasFlag(t, BF_OPAQUE);
}

static bool isBlockFiltering(block_type t) {
    return hasFlag(t, BF_TRANSLUCENT);
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
    // Write packed: sky in high nibble, block light stays 0
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint8_t light = 15;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                if (y < scanH) {
                    uint8_t info = blockInfo[biIdx(x, y, z)];
                    if (info & 1) break;                               // opaque
                    if (info & 2) light = (light > 1) ? light - 1 : 0; // filtering
                }
                skyLight[slIdx(x, y, z)] = light << 4;
            }
        }
    }

    // Phase 2: BFS flood fill — only seed from lit blocks at shadow edges
    // Use packed int32 queue instead of tuple for cache efficiency
    std::vector<int32_t> queue;
    queue.reserve(CHUNK_SIZE * CHUNK_SIZE * 2);
    auto packCoord = [](int x, int y, int z) -> int32_t { return (x << 20) | (y << 8) | z; };

    // Only seed blocks that are lit AND have at least one dark/unlit neighbor
    int seedMaxY = std::min(maxSolidY + 2, CHUNK_HEIGHT);
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < seedMaxY; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                uint8_t mySky = unpackSky(skyLight[slIdx(x, y, z)]);
                if (mySky <= 1) continue;
                // Check if any neighbor is darker (worth seeding from)
                bool hasEdge = (x == 0 || x == CHUNK_SIZE - 1 || z == 0 || z == CHUNK_SIZE - 1);
                if (!hasEdge) {
                    hasEdge = (y > 0 && unpackSky(skyLight[slIdx(x, y - 1, z)]) < mySky - 1) ||
                              (unpackSky(skyLight[slIdx(x - 1 < 0 ? 0 : x - 1, y, z)]) < mySky - 1) ||
                              (unpackSky(skyLight[slIdx(x + 1 >= CHUNK_SIZE ? CHUNK_SIZE - 1 : x + 1, y, z)]) < mySky - 1) ||
                              (unpackSky(skyLight[slIdx(x, y, z - 1 < 0 ? 0 : z - 1)]) < mySky - 1) ||
                              (unpackSky(skyLight[slIdx(x, y, z + 1 >= CHUNK_SIZE ? CHUNK_SIZE - 1 : z + 1)]) < mySky - 1);
                }
                if (hasEdge) queue.push_back(packCoord(x, y, z));
            }

    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    size_t head = 0;
    while (head < queue.size()) {
        int32_t packed = queue[head++];
        int x = packed >> 20, y = (packed >> 8) & 0xFFF, z = packed & 0xFF;
        uint8_t light = unpackSky(skyLight[slIdx(x, y, z)]);
        if (light <= 1) continue;

        for (auto& d : DIRS) {
            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= scanH || nz < 0 || nz >= CHUNK_SIZE) continue;

            if (blockInfo[biIdx(nx, ny, nz)] & 1) continue; // opaque

            uint8_t newLight = light - 1;
            if (unpackSky(skyLight[slIdx(nx, ny, nz)]) >= newLight) continue;
            size_t ni = slIdx(nx, ny, nz);
            skyLight[ni] = (newLight << 4) | (skyLight[ni] & 0xF);
            queue.push_back(packCoord(nx, ny, nz));
        }
    }
}

void Chunk::computeSkyLight() {
    auto flat = decompressBlocks();
    computeSkyLightData(flat.get(), skyLight.get(), maxSolidY);
}

uint8_t Chunk::getSkyLight(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 15;
    return unpackSky(skyLight[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
}

uint8_t Chunk::getBlockLight(int x, int y, int z) const {
    if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
    return unpackBlock(skyLight[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
}

void Chunk::propagateBorderLight(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
    auto idx = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
    };
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    std::vector<int32_t> skyQueue, blkQueue;
    skyQueue.reserve(CHUNK_SIZE * CHUNK_HEIGHT * 2);
    blkQueue.reserve(CHUNK_SIZE * CHUNK_HEIGHT);
    auto packCoord = [](int x, int y, int z) -> int32_t { return (x << 20) | (y << 8) | z; };

    uint8_t* lt = skyLight.get(); // packed light array
    auto flatBlocks = decompressBlocks();
    Cube* blks = flatBlocks.get();

    // Seed from neighbor borders: for each border face, check if the neighbor's
    // light value minus 1 is greater than our edge block's current light.
    // If so, seed it and add to BFS queue.
    auto seedEdge = [&](Chunk* neighbor, int selfX, int selfZ, int nbrX, int nbrZ) {
        if (!neighbor) return;
        int scanH = std::min(maxSolidY + 16, CHUNK_HEIGHT);
        for (int y = 0; y < scanH; y++) {
            size_t si = idx(selfX, y, selfZ);
            // Sky light
            uint8_t nbrSky = neighbor->getSkyLight(nbrX, y, nbrZ);
            if (nbrSky > 1) {
                uint8_t propagated = nbrSky - 1;
                if (propagated > unpackSky(lt[si]) &&
                    !hasFlag(blks[si].getType(), BF_OPAQUE)) {
                    lt[si] = (propagated << 4) | (lt[si] & 0xF);
                    skyQueue.push_back(packCoord(selfX, y, selfZ));
                }
            }
            // Block light
            uint8_t nbrBlk = neighbor->getBlockLight(nbrX, y, nbrZ);
            if (nbrBlk > 1) {
                uint8_t propagated = nbrBlk - 1;
                if (propagated > unpackBlock(lt[si]) &&
                    !hasFlag(blks[si].getType(), BF_OPAQUE)) {
                    lt[si] = (lt[si] & 0xF0) | (propagated & 0xF);
                    blkQueue.push_back(packCoord(selfX, y, selfZ));
                }
            }
        }
    };

    // Seed from -X neighbor (their x=CHUNK_SIZE-1 -> our x=0)
    if (nx_neg) {
        for (int z = 0; z < CHUNK_SIZE; z++)
            seedEdge(nx_neg, 0, z, CHUNK_SIZE - 1, z);
    }
    // Seed from +X neighbor (their x=0 -> our x=CHUNK_SIZE-1)
    if (nx_pos) {
        for (int z = 0; z < CHUNK_SIZE; z++)
            seedEdge(nx_pos, CHUNK_SIZE - 1, z, 0, z);
    }
    // Seed from -Z neighbor (their z=CHUNK_SIZE-1 -> our z=0)
    if (nz_neg) {
        for (int x = 0; x < CHUNK_SIZE; x++)
            seedEdge(nz_neg, x, 0, x, CHUNK_SIZE - 1);
    }
    // Seed from +Z neighbor (their z=0 -> our z=CHUNK_SIZE-1)
    if (nz_pos) {
        for (int x = 0; x < CHUNK_SIZE; x++)
            seedEdge(nz_pos, x, CHUNK_SIZE - 1, x, 0);
    }

    // BFS flood inward — sky light uses high nibble, block light uses low nibble
    auto floodBFSSky = [&](std::vector<int32_t>& q) {
        size_t head = 0;
        while (head < q.size()) {
            int32_t packed = q[head++];
            int x = packed >> 20, y = (packed >> 8) & 0xFFF, z = packed & 0xFF;
            uint8_t light = unpackSky(lt[idx(x, y, z)]);
            if (light <= 1) continue;
            for (auto& d : DIRS) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
                block_type bt = blks[idx(nx, ny, nz)].getType();
                if (hasFlag(bt, BF_OPAQUE)) continue;
                uint8_t newLight = light - 1;
                size_t ni = idx(nx, ny, nz);
                if (unpackSky(lt[ni]) >= newLight) continue;
                lt[ni] = (newLight << 4) | (lt[ni] & 0xF);
                q.push_back(packCoord(nx, ny, nz));
            }
        }
    };
    auto floodBFSBlock = [&](std::vector<int32_t>& q) {
        size_t head = 0;
        while (head < q.size()) {
            int32_t packed = q[head++];
            int x = packed >> 20, y = (packed >> 8) & 0xFFF, z = packed & 0xFF;
            uint8_t light = unpackBlock(lt[idx(x, y, z)]);
            if (light <= 1) continue;
            for (auto& d : DIRS) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
                block_type bt = blks[idx(nx, ny, nz)].getType();
                if (hasFlag(bt, BF_OPAQUE) && getBlockLightEmission(bt) == 0) continue;
                uint8_t newLight = light - 1;
                size_t ni = idx(nx, ny, nz);
                if (unpackBlock(lt[ni]) >= newLight) continue;
                lt[ni] = (lt[ni] & 0xF0) | (newLight & 0xF);
                q.push_back(packCoord(nx, ny, nz));
            }
        }
    };
    floodBFSSky(skyQueue);
    floodBFSBlock(blkQueue);
}

Cube* Chunk::getBlock(int i, int j, int k) {
    if (i < 0 || i >= CHUNK_SIZE || j < 0 || j >= CHUNK_HEIGHT || k < 0 || k >= CHUNK_SIZE) return nullptr;
    // Use a thread-local scratch cube to return a pointer (callers should migrate to getBlockType)
    thread_local Cube scratch;
    scratch.setType(getBlockType(i, j, k));
    return &scratch;
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
    ZoneScopedN("Chunk::buildMeshData");
    double _buildStart = glfwGetTime();

    // Snapshot which sections we're about to rebuild so uploadMesh
    // clears only these bits (new dirty bits set during the build survive).
    builtDirtyMask = sectionDirty;

    // Decompress sections into flat buffer for mesh building
    auto flatBlocks = decompressBlocks();
    Cube* blocks = flatBlocks.get();

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
    // Packed vertex layout (12 bytes): pos(3×int16) + uv(2×uint8) + normalIdx(uint8) + texLayer(uint8) + ao(uint8) + packedLight(uint8)

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

    // Pre-cache packed light for fast per-vertex access (high nibble = sky, low nibble = block)
    uint8_t* lightPtr = skyLight.get();
    auto slDirect = [lightPtr](int x, int y, int z) -> int {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 15;
        return unpackSky(lightPtr[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
    };
    auto blDirect = [lightPtr](int x, int y, int z) -> int {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
        return unpackBlock(lightPtr[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
    };

    int mask[MAX_DIM][MAX_DIM];
    const float worldOff[3] = {(float)(chunkX * CHUNK_SIZE), 0.0f, (float)(chunkY * CHUNK_SIZE)};

    // Effective dimensions capped by maxSolidY+1 (skip pure-air region above)
    const int effDIM[3] = {CHUNK_SIZE, maxSolidY + 2, CHUNK_SIZE};

    // ---- Per-section rebuild loop ----
    // Only rebuild dirty sections; clean sections keep their cached meshes.
    for (int sect = 0; sect < NUM_SECTIONS; sect++) {
        if (!(sectionDirty & (1u << sect))) continue;
        int yMin = sect * 16;
        int yMax = std::min(yMin + 16, (int)CHUNK_HEIGHT);

        auto& sm = sectionMeshes[sect];
        sm.verts.clear();
        sm.opaqueIdx.clear();
        sm.waterIdx.clear();
        // Single vertex counter — opaque and water faces share sm.verts,
        // so their indices must reference the same vertex space.
        unsigned int vertBase = 0;

    for (int f = 0; f < 6; f++) {
        const FaceDef& fd = FACE_DEFS[f];
        const glm::vec3& norm = FACE_NORMALS[f];
        const int d_dim = std::min(DIM[fd.d], effDIM[fd.d]);
        const int u_dim = std::min(DIM[fd.u], effDIM[fd.u]);
        const int v_dim = std::min(DIM[fd.v], effDIM[fd.v]);

        // Clamp the Y-related axis to the current section's range.
        const int d_start = (fd.d == 1) ? std::max(yMin, 0) : 0;
        const int d_end   = (fd.d == 1) ? std::min(yMax, d_dim) : d_dim;
        const int v_start = (fd.v == 1) ? std::max(yMin, 0) : 0;
        const int v_end   = (fd.v == 1) ? std::min(yMax, v_dim) : v_dim;

        for (int d = d_start; d < d_end; d++) {

            // 1. Build mask for this face direction and slice
            bool anyFace = false;
            for (int u = 0; u < u_dim; u++) {
                for (int v = v_start; v < v_end; v++) {
                    int c[3];
                    c[fd.d] = d;
                    c[fd.u] = u;
                    c[fd.v] = v;
                    block_type bt = getBlock(c[0], c[1], c[2])->getType();
                    // Skip air blocks entirely.
                    if (bt == AIR) {
                        mask[u][v] = -1;
                        continue;
                    }

                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    Cube* nb = getBlockCross(this, nc[0], nc[1], nc[2], nx_neg, nx_pos, nz_neg, nz_pos);
                    block_type nbType = nb ? nb->getType() : STONE;

                    bool show;
                    if (hasFlag(bt, BF_LIQUID)) {
                        // Water renders top face (f==4) always if neighbor
                        // above is air. Side faces render where water meets
                        // air — this gives the visible "edge" of flowing water.
                        // Skip face between two water blocks (no visible boundary).
                        show = (nbType == AIR);
                    } else {
                        show = (nbType == AIR) ||
                               (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID)) ||
                               (g_fancyLeaves && (hasFlag(bt, BF_TRANSLUCENT) || hasFlag(nbType, BF_TRANSLUCENT)));
                    }
                    int val = show ? (int)bt : -1;
                    mask[u][v] = val;
                    if (val != -1) anyFace = true;
                }
            }

            if (!anyFace) continue; // entire slice is air, skip greedy sweep

            // 2. Sweep mask and emit quads
            for (int u = 0; u < u_dim; u++) {
                for (int v = v_start; v < v_end;) {
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

                    // Greedy merge must not cross section boundary along the v
                    // axis (which is Y for side faces).
                    {
                        int h_max = v_end - v;
                        if (h > h_max) h = h_max;
                    }

                    // Water top face: per-vertex heights averaged with neighbors for a smooth surface.
                    float waterY[4] = {d_val, d_val, d_val, d_val};
                    if (bt == (int)WATER && f == 4) {
                        computeWaterTopCorners(blocks, waterLevels.get(), u, d, v, fd.u_sign, fd.v_sign, waterY);
                    }

                    float u_lo = (float)u - 0.5f, u_hi = (float)(u + w) - 0.5f;
                    float v_lo = (float)v - 0.5f, v_hi = (float)(v + h) - 0.5f;
                    float u0 = fd.u_sign > 0 ? u_lo : u_hi;
                    float u1 = fd.u_sign > 0 ? u_hi : u_lo;
                    float v0 = fd.v_sign > 0 ? v_lo : v_hi;
                    float v1 = fd.v_sign > 0 ? v_hi : v_lo;

                    float vp[4][3];
                    bool isWT = (bt == (int)WATER && f == 4);

                    // Water side faces: clamp the top edge (high-v vertices)
                    // to the water surface height so sides aren't full-block.
                    // fd.v==1 means the v axis is Y (true for all side faces).
                    bool isWaterSide = (bt == (int)WATER && f != 4 && f != 5 && fd.v == 1);
                    if (isWaterSide && waterLevels) {
                        size_t wi = static_cast<size_t>(u) * CHUNK_HEIGHT * CHUNK_SIZE + d * CHUNK_SIZE + v;
                        uint8_t wraw = waterLevels.get()[wi];
                        float wh = waterIsFalling(wraw) ? (8.0f / 9.0f) : ((8.0f - waterFlowLevel(wraw)) / 9.0f);
                        // v1 is the high-Y edge; clamp it to water height
                        float waterTop = (float)v - 0.5f + wh;
                        if (fd.v_sign > 0) v1 = waterTop;
                        else v0 = waterTop;
                    }

                    // Vertex order: (u0,v0), (u0,v1), (u1,v1), (u1,v0)
                    vp[0][fd.d] = isWT ? waterY[0] : d_val;
                    vp[0][fd.u] = u0;
                    vp[0][fd.v] = v0;
                    vp[1][fd.d] = isWT ? waterY[1] : d_val;
                    vp[1][fd.u] = u0;
                    vp[1][fd.v] = v1;
                    vp[2][fd.d] = isWT ? waterY[2] : d_val;
                    vp[2][fd.u] = u1;
                    vp[2][fd.v] = v1;
                    vp[3][fd.d] = isWT ? waterY[3] : d_val;
                    vp[3][fd.u] = u1;
                    vp[3][fd.v] = v0;

                    const float uvs[4][2] = {{0.f, 0.f}, {0.f, (float)h}, {(float)w, (float)h}, {(float)w, 0.f}};

                    // Per-vertex AO: check 3 corner neighbors per vertex
                    // For each vertex, find the block at that corner and the
                    // two side + one diagonal neighbor in the face's normal direction.
                    int skyLightVals[4], blockLightVals[4];
                    int bu[4], bv[4], cu[4], cv[4];
                    bu[0] = bu[1] = (fd.u_sign > 0) ? u : u + w - 1;
                    bu[2] = bu[3] = (fd.u_sign > 0) ? u + w - 1 : u;
                    bv[0] = bv[3] = (fd.v_sign > 0) ? v : v + h - 1;
                    bv[1] = bv[2] = (fd.v_sign > 0) ? v + h - 1 : v;
                    cu[0] = cu[1] = -fd.u_sign;
                    cu[2] = cu[3] = fd.u_sign;
                    cv[0] = cv[3] = -fd.v_sign;
                    cv[1] = cv[2] = fd.v_sign;

                    int ao[4];
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

                        ao[vi] = aoVal;

                        skyLightVals[vi] = slDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                        blockLightVals[vi] = blDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                    }

                    bool isWater = (bt == (int)WATER);

                    // Compute flow direction for water top faces.
                    // Packed into the ao byte's upper bits so the
                    // fragment shader can scroll the UV directionally.
                    uint8_t flowBits = 0;
                    if (isWater && f == 4 && waterLevels) {
                        size_t wi = static_cast<size_t>(u) * CHUNK_HEIGHT * CHUNK_SIZE + d * CHUNK_SIZE + v;
                        uint8_t raw = waterLevels.get()[wi];
                        if (!waterIsSource(raw) && !waterIsFalling(raw)) {
                            int myLvl = waterFlowLevel(raw);
                            float fx = 0, fz = 0;
                            // +X / -X neighbors
                            for (int dx : {-1, 1}) {
                                int nx = u + dx;
                                block_type nbt; uint8_t nraw = 0;
                                if (nx >= 0 && nx < CHUNK_SIZE) {
                                    nbt = getBlock(nx, d, v)->getType();
                                    if (nbt == WATER) nraw = waterLevels.get()[static_cast<size_t>(nx) * CHUNK_HEIGHT * CHUNK_SIZE + d * CHUNK_SIZE + v];
                                } else {
                                    Cube* nb = getBlockCross(this, nx, d, v, nx_neg, nx_pos, nz_neg, nz_pos);
                                    nbt = nb ? nb->getType() : STONE;
                                    if (nbt == WATER) {
                                        Chunk* nc = (dx < 0) ? nx_neg : nx_pos;
                                        if (nc) nraw = nc->getWaterLevel(dx < 0 ? CHUNK_SIZE - 1 : 0, d, v);
                                    }
                                }
                                if (nbt == AIR) { fx += dx * 8; }
                                else if (nbt == WATER && !waterIsSource(nraw) && waterFlowLevel(nraw) > myLvl) {
                                    fx += dx * (waterFlowLevel(nraw) - myLvl);
                                }
                            }
                            // +Z / -Z neighbors
                            for (int dz : {-1, 1}) {
                                int nz = v + dz;
                                block_type nbt; uint8_t nraw = 0;
                                if (nz >= 0 && nz < CHUNK_SIZE) {
                                    nbt = getBlock(u, d, nz)->getType();
                                    if (nbt == WATER) nraw = waterLevels.get()[static_cast<size_t>(u) * CHUNK_HEIGHT * CHUNK_SIZE + d * CHUNK_SIZE + nz];
                                } else {
                                    Cube* nb = getBlockCross(this, u, d, nz, nx_neg, nx_pos, nz_neg, nz_pos);
                                    nbt = nb ? nb->getType() : STONE;
                                    if (nbt == WATER) {
                                        Chunk* nc = (dz < 0) ? nz_neg : nz_pos;
                                        if (nc) nraw = nc->getWaterLevel(u, d, dz < 0 ? CHUNK_SIZE - 1 : 0);
                                    }
                                }
                                if (nbt == AIR) { fz += dz * 8; }
                                else if (nbt == WATER && !waterIsSource(nraw) && waterFlowLevel(nraw) > myLvl) {
                                    fz += dz * (waterFlowLevel(nraw) - myLvl);
                                }
                            }
                            if (fx != 0 || fz != 0) {
                                int angleIdx = ((int)roundf(atan2f(fz, fx) / (3.14159265f / 8.0f)) + 16) % 16;
                                flowBits = FLOW_HAS_DIR_BIT | ((angleIdx & FLOW_ANGLE_MASK) << FLOW_ANGLE_SHIFT);
                            }
                        }
                    }

                    auto& idx = isWater ? sm.waterIdx : sm.opaqueIdx;

                    // Batch-write 4 packed vertices at once
                    size_t off = sm.verts.size();
                    sm.verts.resize(off + 4 * BYTES_PER_VERT);
                    PackedVertex* dst = reinterpret_cast<PackedVertex*>(&sm.verts[off]);
                    for (int vi = 0; vi < 4; vi++) {
                        dst->px = (int16_t)((vp[vi][0] + worldOff[0]) * 2.0f);
                        dst->py = (int16_t)((vp[vi][1] + worldOff[1]) * 2.0f);
                        dst->pz = (int16_t)((vp[vi][2] + worldOff[2]) * 2.0f);
                        dst->u = (uint8_t)uvs[vi][0];
                        dst->v = (uint8_t)uvs[vi][1];
                        dst->normalIdx = (uint8_t)f;
                        dst->texLayer = (uint8_t)layer;
                        dst->ao = (uint8_t)(ao[vi] & 0x03) | flowBits;
                        dst->packedLight = (uint8_t)(skyLightVals[vi] * 16 + blockLightVals[vi]);
                        dst++;
                    }
                    // Flip quad diagonal when AO is asymmetric to avoid interpolation artifacts
                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        idx.push_back(vertBase);
                        idx.push_back(vertBase + 1);
                        idx.push_back(vertBase + 2);
                        idx.push_back(vertBase + 2);
                        idx.push_back(vertBase + 3);
                        idx.push_back(vertBase);
                    } else {
                        idx.push_back(vertBase + 1);
                        idx.push_back(vertBase + 2);
                        idx.push_back(vertBase + 3);
                        idx.push_back(vertBase + 3);
                        idx.push_back(vertBase);
                        idx.push_back(vertBase + 1);
                    }
                    vertBase += 4;

                    if (g_greedyMeshing) {
                        for (int du = 0; du < w; du++)
                            for (int dv = 0; dv < h; dv++) mask[u + du][v + dv] = -1;
                    }
                    v += h;
                }
            }
        }
    }
    } // end per-section loop

    // ---- Stitch all section meshes into one pendingMesh ----
    // Concatenate per-section verts and rebase indices so the GPU sees
    // one continuous VBO/EBO with opaque triangles first, then water.
    {
        size_t totalVerts = 0, totalOpaque = 0, totalWater = 0;
        for (int s = 0; s < NUM_SECTIONS; s++) {
            totalVerts += sectionMeshes[s].verts.size();
            totalOpaque += sectionMeshes[s].opaqueIdx.size();
            totalWater += sectionMeshes[s].waterIdx.size();
        }

        pendingMesh.verts.clear();
        pendingMesh.verts.reserve(totalVerts);
        pendingMesh.opaqueIdx.clear();
        pendingMesh.opaqueIdx.reserve(totalOpaque + totalWater);
        pendingMesh.waterIdx.clear();

        unsigned int vertOffset = 0;
        // Opaque indices from each section
        for (int s = 0; s < NUM_SECTIONS; s++) {
            auto& sm = sectionMeshes[s];
            pendingMesh.verts.insert(pendingMesh.verts.end(), sm.verts.begin(), sm.verts.end());
            for (auto idx_val : sm.opaqueIdx)
                pendingMesh.opaqueIdx.push_back(idx_val + vertOffset);
            // Water indices get a second pass (opaque first in the EBO)
            vertOffset += (unsigned int)(sm.verts.size() / BYTES_PER_VERT);
        }
        // Water indices from each section (rebase with same vertex offsets)
        unsigned int vertOffset2 = 0;
        for (int s = 0; s < NUM_SECTIONS; s++) {
            auto& sm = sectionMeshes[s];
            for (auto idx_val : sm.waterIdx)
                pendingMesh.opaqueIdx.push_back(idx_val + vertOffset2);
            vertOffset2 += (unsigned int)(sm.verts.size() / BYTES_PER_VERT);
        }
        pendingMesh.waterIdx.resize(totalWater);
        pendingMesh.ready = true;
    }

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
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                nb.xNeg.types[z][y] = nx_neg->getBlock(CHUNK_SIZE - 1, y, z)->getType();
                nb.xNeg.lightBorder[z][y] = nx_neg->skyLight[lightIdx(CHUNK_SIZE - 1, y, z)];
            }
    }
    if (nx_pos) {
        nb.xPos.valid = true;
        for (int z = 0; z < CHUNK_SIZE; z++)
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                nb.xPos.types[z][y] = nx_pos->getBlock(0, y, z)->getType();
                nb.xPos.lightBorder[z][y] = nx_pos->skyLight[lightIdx(0, y, z)];
            }
    }
    if (nz_neg) {
        nb.zNeg.valid = true;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                nb.zNeg.types[x][y] = nz_neg->getBlock(x, y, CHUNK_SIZE - 1)->getType();
                nb.zNeg.lightBorder[x][y] = nz_neg->skyLight[lightIdx(x, y, CHUNK_SIZE - 1)];
            }
    }
    if (nz_pos) {
        nb.zPos.valid = true;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                nb.zPos.types[x][y] = nz_pos->getBlock(x, y, 0)->getType();
                nb.zPos.lightBorder[x][y] = nz_pos->skyLight[lightIdx(x, y, 0)];
            }
    }
    return nb;
}

// Free function: build mesh from raw data — fully thread-safe, no GL calls
Chunk::MeshData buildMeshFromData(Cube* blocks, uint8_t* light, uint8_t* waterLevels, int maxSolidY, int chunkX,
                                  int chunkZ, const Chunk::NeighborBorders& nb) {
    ZoneScopedN("buildMeshFromData");
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
    struct FaceDef {
        int d, u, v;
        int d_sign, u_sign, v_sign;
    };
    static constexpr FaceDef FACE_DEFS[6] = {
        {2, 0, 1, 1, 1, 1},  {2, 0, 1, -1, -1, 1}, {0, 2, 1, -1, 1, 1},
        {0, 2, 1, 1, -1, 1}, {1, 0, 2, 1, 1, -1},  {1, 0, 2, -1, 1, 1},
    };
    static constexpr int DIM[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
    static constexpr int MAX_DIM = CHUNK_HEIGHT > CHUNK_SIZE ? CHUNK_HEIGHT : CHUNK_SIZE;

    constexpr size_t MAX_FACES = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * 4;
    std::vector<uint8_t> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * BYTES_PER_VERT);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4 * BYTES_PER_VERT);
    waterIdx.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

    auto slDirect = [light](int x, int y, int z) -> int {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 15;
        return unpackSky(light[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
    };
    auto blDirect = [light](int x, int y, int z) -> int {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) return 0;
        return unpackBlock(light[static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z]);
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
                    int c[3];
                    c[fd.d] = d;
                    c[fd.u] = u;
                    c[fd.v] = v;
                    block_type bt = blocks[c[0] * CHUNK_HEIGHT * CHUNK_SIZE + c[1] * CHUNK_SIZE + c[2]].getType();
                    if (bt == AIR) {
                        mask[u][v] = -1;
                        continue;
                    }
                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    block_type nbType = getTypeCross(nc[0], nc[1], nc[2]);
                    bool show;
                    if (hasFlag(bt, BF_LIQUID)) {
                        show = (nbType == AIR);
                    } else {
                        show = (nbType == AIR) ||
                               (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID)) ||
                               (g_fancyLeaves && (hasFlag(bt, BF_TRANSLUCENT) || hasFlag(nbType, BF_TRANSLUCENT)));
                    }
                    int val = show ? (int)bt : -1;
                    mask[u][v] = val;
                    if (val != -1) anyFace = true;
                }
            }
            if (!anyFace) continue;

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

                    float waterY2[4] = {d_val, d_val, d_val, d_val};
                    if (bt == (int)WATER && f == 4) {
                        computeWaterTopCorners(blocks, waterLevels, u, d, v, fd.u_sign, fd.v_sign, waterY2);
                    }

                    float u_lo = (float)u - 0.5f, u_hi = (float)(u + w) - 0.5f;
                    float v_lo = (float)v - 0.5f, v_hi = (float)(v + h) - 0.5f;
                    float u0 = fd.u_sign > 0 ? u_lo : u_hi;
                    float u1 = fd.u_sign > 0 ? u_hi : u_lo;
                    float v0 = fd.v_sign > 0 ? v_lo : v_hi;
                    float v1 = fd.v_sign > 0 ? v_hi : v_lo;

                    float vp[4][3];
                    bool isWT2 = (bt == (int)WATER && f == 4);
                    vp[0][fd.d] = isWT2 ? waterY2[0] : d_val;
                    vp[0][fd.u] = u0;
                    vp[0][fd.v] = v0;
                    vp[1][fd.d] = isWT2 ? waterY2[1] : d_val;
                    vp[1][fd.u] = u0;
                    vp[1][fd.v] = v1;
                    vp[2][fd.d] = isWT2 ? waterY2[2] : d_val;
                    vp[2][fd.u] = u1;
                    vp[2][fd.v] = v1;
                    vp[3][fd.d] = isWT2 ? waterY2[3] : d_val;
                    vp[3][fd.u] = u1;
                    vp[3][fd.v] = v0;

                    const float uvs[4][2] = {{0.f, 0.f}, {0.f, (float)h}, {(float)w, (float)h}, {(float)w, 0.f}};

                    int skyLightVals[4], blockLightVals[4];
                    int bu[4], bv[4], cu[4], cv[4];
                    bu[0] = bu[1] = (fd.u_sign > 0) ? u : u + w - 1;
                    bu[2] = bu[3] = (fd.u_sign > 0) ? u + w - 1 : u;
                    bv[0] = bv[3] = (fd.v_sign > 0) ? v : v + h - 1;
                    bv[1] = bv[2] = (fd.v_sign > 0) ? v + h - 1 : v;
                    cu[0] = cu[1] = -fd.u_sign;
                    cu[2] = cu[3] = fd.u_sign;
                    cv[0] = cv[3] = -fd.v_sign;
                    cv[1] = cv[2] = fd.v_sign;

                    int ao[4];
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
                        ao[vi] = aoVal;
                        skyLightVals[vi] = slDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                        blockLightVals[vi] = blDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                    }

                    bool isWater = (bt == (int)WATER);
                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx = isWater ? waterIdx : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    size_t off = verts.size();
                    verts.resize(off + 4 * BYTES_PER_VERT);
                    PackedVertex* dst = reinterpret_cast<PackedVertex*>(&verts[off]);
                    for (int vi = 0; vi < 4; vi++) {
                        dst->px = (int16_t)((vp[vi][0] + worldOff[0]) * 2.0f);
                        dst->py = (int16_t)((vp[vi][1] + worldOff[1]) * 2.0f);
                        dst->pz = (int16_t)((vp[vi][2] + worldOff[2]) * 2.0f);
                        dst->u = (uint8_t)uvs[vi][0];
                        dst->v = (uint8_t)uvs[vi][1];
                        dst->normalIdx = (uint8_t)f;
                        dst->texLayer = (uint8_t)layer;
                        dst->ao = (uint8_t)ao[vi];
                        dst->packedLight = (uint8_t)(skyLightVals[vi] * 16 + blockLightVals[vi]);
                        dst++;
                    }

                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        idx.push_back(base);
                        idx.push_back(base + 1);
                        idx.push_back(base + 2);
                        idx.push_back(base + 2);
                        idx.push_back(base + 3);
                        idx.push_back(base);
                    } else {
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
    auto start = std::chrono::steady_clock::now();
    auto flat = decompressBlocks();
    pendingMesh = buildMeshFromData(flat.get(), skyLight.get(), waterLevels.get(), maxSolidY, chunkX, chunkY, borders);
    auto end = std::chrono::steady_clock::now();
    g_frame.meshBuildMs += std::chrono::duration<double, std::milli>(end - start).count();
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
    glBufferData(GL_ARRAY_BUFFER, pendingMesh.verts.size(), pendingMesh.verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunkEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, pendingMesh.opaqueIdx.size() * sizeof(unsigned int),
                 pendingMesh.opaqueIdx.data(), GL_STATIC_DRAW);

    constexpr int STRIDE = 12;
    // pos: 3 x int16 at offset 0
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    // uv: 2 x uint8 at offset 6
    glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_FALSE, STRIDE, (void*)6);
    glEnableVertexAttribArray(1);
    // normalIdx: 1 x uint8 at offset 8
    glVertexAttribPointer(2, 1, GL_UNSIGNED_BYTE, GL_FALSE, STRIDE, (void*)8);
    glEnableVertexAttribArray(2);
    // texLayer: 1 x uint8 at offset 9
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, STRIDE, (void*)9);
    glEnableVertexAttribArray(3);
    // ao: 1 x uint8 at offset 10
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_FALSE, STRIDE, (void*)10);
    glEnableVertexAttribArray(4);
    // packedLight: 1 x uint8 at offset 11
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_FALSE, STRIDE, (void*)11);
    glEnableVertexAttribArray(5);

    glBindVertexArray(0);

    // Track counts before clearing
    int totalOpaqueIdx = (int)pendingMesh.opaqueIdx.size() - (int)pendingMesh.waterIdx.size();
    opaqueIndexCount = totalOpaqueIdx;
    waterIndexCount = (int)pendingMesh.waterIdx.size();
    waterIndexOffset = totalOpaqueIdx * sizeof(unsigned int);
    // Clear only the dirty bits that the uploaded mesh was built from.
    // New bits set since buildMeshData ran (e.g., by the water sim)
    // survive and trigger another rebuild next frame.
    sectionDirty &= ~builtDirtyMask;
    builtDirtyMask = 0;

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
    } else if (isMeshDirty() && !meshBuildInFlight && g_frame.meshBuildBudget > 0) {
        // Fallback: build on main thread if not queued for async
        buildMesh(nx_neg, nx_pos, nz_neg, nz_pos);
        uploadMesh();
        g_frame.meshBuildBudget--;
    }

    if (opaqueIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, opaqueIndexCount, GL_UNSIGNED_INT, nullptr);
        g_frame.opaqueTriangles += opaqueIndexCount / 3;
        g_frame.vertexCount += opaqueIndexCount / 6 * 4;
        g_frame.opaqueDrawCalls++;
    }
    return {};
}

void Chunk::renderWater(const Shader& /*shaderProgram*/, Chunk* /*nx_neg*/, Chunk* /*nx_pos*/, Chunk* /*nz_neg*/,
                        Chunk* /*nz_pos*/) {
    if (waterIndexCount > 0) {
        glBindVertexArray(chunkVAO);
        glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT, (void*)waterIndexOffset);
        g_frame.waterTriangles += waterIndexCount / 3;
        g_frame.waterDrawCalls++;
    }
}

void Chunk::destroyBlock(int x, int y, int z) {
    // setBlockType marks the affected section (+ boundary neighbors) dirty.
    setBlockType(x, y, z, AIR);
}

// Sky light darkening: light removal BFS when a block is placed
static void darkenSkyLightLocal(Cube* blocks, uint8_t* skyLight, int x, int y, int z) {
    auto idx = [](int bx, int by, int bz) -> size_t {
        return static_cast<size_t>(bx) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(by) * CHUNK_SIZE + bz;
    };
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    // Phase 1: zero the placed block's sky light and any direct sky column below it
    { size_t i = idx(x, y, z); skyLight[i] = skyLight[i] & 0xF; } // zero sky nibble, keep block

    struct Node {
        int x, y, z;
        uint8_t oldLight;
    };
    std::vector<Node> removeQueue;
    removeQueue.reserve(256);

    // Walk column downward — block breaks the sky column
    for (int by = y - 1; by >= 0; by--) {
        if (hasFlag(blocks[idx(x, by, z)].getType(), BF_OPAQUE)) break;
        size_t bi = idx(x, by, z);
        if (unpackSky(skyLight[bi]) != 15) break;
        uint8_t old = unpackSky(skyLight[bi]);
        skyLight[bi] = skyLight[bi] & 0xF; // zero sky nibble
        removeQueue.push_back({x, by, z, old});
    }

    // Seed removal from the placed block's neighbors too
    for (auto& d : DIRS) {
        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
        if (hasFlag(blocks[idx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
        // Only seed neighbors that had less than max light (those derived from this block)
    }

    // Phase 2: BFS light removal from zeroed positions
    std::vector<Node> relightSeeds;
    relightSeeds.reserve(64);
    size_t head = 0;
    while (head < removeQueue.size()) {
        auto [cx, cy, cz, oldLight] = removeQueue[head++];
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[idx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            size_t ni = idx(nx, ny, nz);
            uint8_t neighborLight = unpackSky(skyLight[ni]);
            if (neighborLight > 0 && neighborLight < oldLight) {
                skyLight[ni] = skyLight[ni] & 0xF; // zero sky nibble
                removeQueue.push_back({nx, ny, nz, neighborLight});
            } else if (neighborLight >= oldLight && neighborLight > 0) {
                relightSeeds.push_back({nx, ny, nz, neighborLight});
            }
        }
    }

    // Phase 3: BFS re-propagation from boundary seeds
    struct LightNode {
        int x, y, z;
    };
    std::vector<LightNode> lightQueue;
    lightQueue.reserve(256);
    for (auto& s : relightSeeds) {
        lightQueue.push_back({s.x, s.y, s.z});
    }
    head = 0;
    while (head < lightQueue.size()) {
        auto [cx, cy, cz] = lightQueue[head++];
        uint8_t light = unpackSky(skyLight[idx(cx, cy, cz)]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (hasFlag(blocks[idx(nx, ny, nz)].getType(), BF_OPAQUE)) continue;
            // Sky column: light=15 propagates downward without attenuation
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            size_t ni = idx(nx, ny, nz);
            if (unpackSky(skyLight[ni]) >= propagated) continue;
            skyLight[ni] = (propagated << 4) | (skyLight[ni] & 0xF);
            lightQueue.push_back({nx, ny, nz});
        }
    }
}

void Chunk::placeBlock(int x, int y, int z, block_type type) {
    // setBlockType marks the affected section (+ boundary neighbors) dirty.
    setBlockType(x, y, z, type);
    setWaterLevel(x, y, z, 0);
    if (y > maxSolidY) maxSolidY = y;
    auto flat = decompressBlocks();
    darkenSkyLightLocal(flat.get(), skyLight.get(), x, y, z);
    // Note: darkenSkyLightLocal may modify light in adjacent sections,
    // so mark all sections dirty for light-affected rebuilds.
    // TODO: track which sections darkenSkyLightLocal touches for finer-grained dirtyness.
    sectionDirty = 0xFF;
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
    for (int i = 0; i < NUM_SECTIONS; i++) sections[i].reset();
    skyLight.reset();
}

Chunk::~Chunk() {
    if (skyLight) destroy();
}
