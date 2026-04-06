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

Chunk::Chunk(int chunkX, int chunkY, TerrainGenerator& terrain) {
    blocks = new Cube[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE];
    skyLight = new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]();
    this->chunkX = chunkX;
    this->chunkY = chunkY;

    constexpr int WATER_LEVEL = CHUNK_HEIGHT / 2; // 32, matches terrain gen sea level

    for (int i = 0; i < CHUNK_SIZE; i++) {
        for (int k = 0; k < CHUNK_SIZE; k++) {
            int globalX = chunkX * CHUNK_SIZE + i;
            int globalZ = chunkY * CHUNK_SIZE + k;

            Biome biome;
            int height = terrain.getHeightAndBiome(globalX, globalZ, biome);
            this->heights[i][k] = height;
            this->biomes[i][k] = biome;
            const BiomeParams& bp = terrain.getBiomeParams(biome);

            int limit_stone = std::max(1, (int)(0.7 * height));

            for (int j = 0; j < CHUNK_HEIGHT; j++) {
                Cube* block = &blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k];
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
    // Water only exists at j <= WATER_LEVEL, so only check blocks up to WATER_LEVEL+1
    for (int i = 0; i < CHUNK_SIZE; i++) {
        block_type shoreBlock = (biomes[i][0] == BIOME_TUNDRA) ? GRAVEL : SAND; // approx per-row
        for (int k = 0; k < CHUNK_SIZE; k++) {
            if (biomes[i][k] == BIOME_TUNDRA)
                shoreBlock = GRAVEL;
            else
                shoreBlock = SAND;

            for (int j = 0; j <= WATER_LEVEL + 1 && j < CHUNK_HEIGHT; j++) {
                Cube* block = getBlock(i, j, k);
                block_type bt = block->getType();
                if (bt != DIRT && bt != GRASS && bt != STONE && bt != GRAVEL) continue;

                // Check if underwater (any water block above in this column)
                if (j < WATER_LEVEL) {
                    Cube* above = getBlock(i, j + 1, k);
                    if (above && above->getType() == WATER) {
                        block->setType(shoreBlock);
                        continue;
                    }
                }
                // Check 6 neighbors for water adjacency
                static const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (auto& d : dirs) {
                    Cube* nb = getBlock(i + d[0], j + d[1], k + d[2]);
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
    uint64_t treeSeed = static_cast<uint64_t>(chunkX) * 73856093ULL ^ static_cast<uint64_t>(chunkY) * 19349663ULL;
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
        int treeGlobalZ = chunkY * CHUNK_SIZE + tz;
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

        int surface = heights[tx][tz];
        if (surface <= TREE_WATER_LEVEL) continue;
        if (surface + totalH >= CHUNK_HEIGHT) continue;
        if (tx - radius < 1 || tx + radius >= CHUNK_SIZE - 1) continue;
        if (tz - radius < 1 || tz + radius >= CHUNK_SIZE - 1) continue;

        Cube* surfaceBlock = getBlock(tx, surface, tz);
        if (!surfaceBlock || surfaceBlock->getType() != tbp.surfaceBlock) continue;
        if (tbp.surfaceBlock != GRASS) continue; // only plant trees on grass

        // Slope check: reject if any neighbor height differs by more than 2
        bool tooSteep = false;
        for (int dx = -1; dx <= 1 && !tooSteep; dx++) {
            for (int dz = -1; dz <= 1 && !tooSteep; dz++) {
                if (dx == 0 && dz == 0) continue;
                int nx = tx + dx, nz = tz + dz;
                if (nx >= 0 && nx < CHUNK_SIZE && nz >= 0 && nz < CHUNK_SIZE) {
                    if (std::abs(heights[nx][nz] - surface) > 2) tooSteep = true;
                }
            }
        }
        if (tooSteep) continue;

        // Trunk
        for (int y = surface + 1; y <= surface + trunkH; y++) {
            Cube* b = getBlock(tx, y, tz);
            if (b) b->setType(WOOD);
        }

        // Canopy: tapered — full radius at bottom, shrinks toward top
        int canopyBase = surface + trunkH;
        for (int ly = 0; ly < layers; ly++) {
            // Taper: radius shrinks as we go up
            int layerR = std::max(1, radius - ly / 2);
            for (int dx = -layerR; dx <= layerR; dx++) {
                for (int dz = -layerR; dz <= layerR; dz++) {
                    // Skip corners for rounder look
                    if (abs(dx) == layerR && abs(dz) == layerR && layerR > 1) continue;
                    if (dx == 0 && dz == 0 && ly == 0) continue; // trunk top
                    int bx = tx + dx, by = canopyBase + ly, bz = tz + dz;
                    if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
                    Cube* b = getBlock(bx, by, bz);
                    if (b && b->getType() == AIR) b->setType(LEAVES);
                }
            }
        }

        // Crown: small cross on top
        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                if (abs(dx) == 1 && abs(dz) == 1) continue; // skip diagonals
                int bx = tx + dx, bz = tz + dz;
                if (bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
                Cube* crown = getBlock(bx, canopyBase + layers, bz);
                if (crown && crown->getType() == AIR) crown->setType(LEAVES);
            }
        }
    }

    // --- Cactus placement in desert biomes ---
    std::uniform_int_distribution<int> cactusDist(2, 4); // height 2-4
    for (int t = 0; t < 3; t++) {
        int cx = posDist(rng);
        int cz = posDist(rng);
        int roll = chanceDist(rng);
        if (roll >= 30) continue; // 30% chance

        if (biomes[cx][cz] != BIOME_DESERT) continue;

        int surface = heights[cx][cz];
        if (surface <= WATER_LEVEL) continue;
        int cactusH = cactusDist(rng);
        if (surface + cactusH >= CHUNK_HEIGHT) continue;

        Cube* surfBlock = getBlock(cx, surface, cz);
        if (!surfBlock || surfBlock->getType() != SAND) continue;

        for (int y = surface + 1; y <= surface + cactusH; y++) {
            Cube* b = getBlock(cx, y, cz);
            if (b && b->getType() == AIR) b->setType(CACTUS);
        }
    }

    // Compute maxSolidY: highest non-AIR block (scan top-down for speed)
    maxSolidY = 0;
    for (int j = CHUNK_HEIGHT - 1; j >= 0; j--) {
        bool found = false;
        for (int i = 0; i < CHUNK_SIZE && !found; i++)
            for (int k = 0; k < CHUNK_SIZE && !found; k++)
                if (getBlock(i, j, k)->getType() != AIR) {
                    maxSolidY = j;
                    found = true;
                }
        if (found) break;
    }

    computeSkyLight();
}

static bool isBlockOpaque(block_type t) {
    return hasFlag(t, BF_OPAQUE);
}

static bool isBlockFiltering(block_type t) {
    return hasFlag(t, BF_FILTERING);
}

void Chunk::computeSkyLight() {
    const size_t total = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
    std::memset(skyLight, 0, total);

    auto slIdx = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
    };

    // Phase 1: vertical ray — light starts at 15, reduced by 1 per filtering block (leaves)
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint8_t light = 15;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                block_type bt = blocks[slIdx(x, y, z)].getType();
                if (isBlockOpaque(bt)) break;
                if (isBlockFiltering(bt)) light = (light > 1) ? light - 1 : 0;
                skyLight[slIdx(x, y, z)] = light;
            }
        }
    }

    // Phase 2: BFS flood fill — light spreads sideways through air, -1 per step
    std::vector<std::tuple<int, int, int>> queue;
    queue.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4);

    // Seed BFS with all blocks at light 15 that have a dark neighbor
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y <= maxSolidY + 1 && y < CHUNK_HEIGHT; y++)
            for (int z = 0; z < CHUNK_SIZE; z++)
                if (skyLight[slIdx(x, y, z)] == 15) queue.emplace_back(x, y, z);

    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    size_t head = 0;
    while (head < queue.size()) {
        auto [x, y, z] = queue[head++];
        uint8_t light = skyLight[slIdx(x, y, z)];
        if (light <= 1) continue;

        for (auto& d : DIRS) {
            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;

            block_type bt = blocks[slIdx(nx, ny, nz)].getType();
            if (isBlockOpaque(bt)) continue;

            uint8_t newLight = light - 1;
            if (skyLight[slIdx(nx, ny, nz)] >= newLight) continue;
            skyLight[slIdx(nx, ny, nz)] = newLight;
            queue.emplace_back(nx, ny, nz);
        }
    }
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

void Chunk::buildMesh(Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg, Chunk* nz_pos) {
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
    std::vector<float> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * 9);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4 * 9);
    waterIdx.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

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
                    block_type nbType = nb ? nb->getType() : AIR;
                    // Show face if neighbor is air or liquid next to solid
                    int val = (!nb || nbType == AIR || (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID))) ? (int)bt : -1;
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

                        // Sky light stored separately — applied in shader, not baked into AO
                        uint8_t sl = getSkyLight(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                        skyLightVals[vi] = 0.15f + 0.85f * (sl / 15.0f);
                    }

                    bool isWater = (bt == (int)WATER);
                    auto& verts = isWater ? waterVerts : opaqueVerts;
                    auto& idx = isWater ? waterIdx : opaqueIdx;
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
                        verts.push_back(ao[vi]);
                        verts.push_back(skyLightVals[vi]);
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

    // Combine into one VBO/EBO (opaque first, then water)
    std::vector<float> allVerts;
    allVerts.insert(allVerts.end(), opaqueVerts.begin(), opaqueVerts.end());
    allVerts.insert(allVerts.end(), waterVerts.begin(), waterVerts.end());

    // Water indices need to be offset by the opaque vertex base
    std::vector<unsigned int> allIdx;
    allIdx.insert(allIdx.end(), opaqueIdx.begin(), opaqueIdx.end());
    for (auto idx_val : waterIdx) allIdx.push_back(idx_val + opaqueBase); // offset water indices past opaque verts

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
    // layout 5: sky light
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(10 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0);

    opaqueIndexCount = static_cast<int>(opaqueIdx.size());
    waterIndexCount = static_cast<int>(waterIdx.size());
    waterIndexOffset = opaqueIdx.size() * sizeof(unsigned int);
    meshDirty = false;

    g_frame.meshBuildMs += (glfwGetTime() - _buildStart) * 1000.0;
    g_frame.meshBuilds++;
}

std::vector<Cube*> Chunk::render(const Shader& /*shaderProgram*/, Chunk* nx_neg, Chunk* nx_pos, Chunk* nz_neg,
                                 Chunk* nz_pos) {
    if (meshDirty && g_frame.meshBuildBudget > 0) {
        buildMesh(nx_neg, nx_pos, nz_neg, nz_pos);
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

void Chunk::destroyBlock(int x, int y, int z) {
    blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].setType(AIR);
    computeSkyLight(); // recompute — broken block may expose new areas to light
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
    delete[] blocks;
    blocks = nullptr;
    delete[] skyLight;
    skyLight = nullptr;
}

Chunk::~Chunk() {
    if (blocks) destroy();
}
