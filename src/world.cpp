#include "world.h"
#include "light_data.h"
#include "profiler.h"
#include <array>
#include <algorithm>
#include <cmath>

// Extract 6 frustum planes from a combined projection*view matrix (Gribb-Hartmann).
// Each plane is vec4(normal.xyz, distance). A point p is inside if dot(plane.xyz, p) + plane.w >= 0.
static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& m) {
    return {{
        // Left, Right, Bottom, Top, Near, Far
        glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]),
        glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]),
        glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]),
        glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]),
        glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]),
        glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]),
    }};
}

// Test an AABB against the frustum. Returns false if the box is completely outside any plane.
static bool aabbInFrustum(const std::array<glm::vec4, 6>& planes, glm::vec3 minP, glm::vec3 maxP) {
    for (auto& p : planes) {
        // Pick the positive vertex (corner most in the direction of the plane normal)
        glm::vec3 pv(p.x >= 0 ? maxP.x : minP.x, p.y >= 0 ? maxP.y : minP.y, p.z >= 0 ? maxP.z : minP.z);
        if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0) return false;
    }
    return true;
}

World::World(unsigned int seed) {
    this->terrainGenerator = new TerrainGenerator(seed, 0.1, 0, CHUNK_HEIGHT);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
}

static void markBorderNeighborsDirty(ChunkManager* cm, int chunkX, int chunkZ, int lx, int lz) {
    if (lx == 0) {
        Chunk* n = cm->getChunk(chunkX - 1, chunkZ);
        if (n) n->markDirty();
    }
    if (lx == CHUNK_SIZE - 1) {
        Chunk* n = cm->getChunk(chunkX + 1, chunkZ);
        if (n) n->markDirty();
    }
    if (lz == 0) {
        Chunk* n = cm->getChunk(chunkX, chunkZ - 1);
        if (n) n->markDirty();
    }
    if (lz == CHUNK_SIZE - 1) {
        Chunk* n = cm->getChunk(chunkX, chunkZ + 1);
        if (n) n->markDirty();
    }
}

void World::destroyBlock(glm::vec3 position) const {
    int bx = (int)std::floor(position.x);
    int by = (int)std::floor(position.y);
    int bz = (int)std::floor(position.z);
    if (by < 0 || by >= CHUNK_HEIGHT) return;

    int chunkX = worldToChunk(bx);
    int chunkZ = worldToChunk(bz);
    int lx = worldToLocal(bx, chunkX);
    int lz = worldToLocal(bz, chunkZ);

    auto chunk = this->chunkManager->getChunk(chunkX, chunkZ);
    if (!chunk) return;
    chunk->destroyBlock(lx, by, lz);
    markBorderNeighborsDirty(chunkManager, chunkX, chunkZ, lx, lz);
}

// World-space BFS: flood block light from a source, crossing chunk boundaries.
// Caches chunk pointer to avoid repeated hash lookups for blocks in the same chunk.
static void floodBlockLight(ChunkManager* cm, int sx, int sy, int sz, uint8_t emission) {
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    auto toIdx = [](int lx, int y, int lz) -> size_t {
        return static_cast<size_t>(lx) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + lz;
    };

    // Resolve world coord to chunk + local, with chunk pointer cache
    int cachedCX = INT_MIN, cachedCZ = INT_MIN;
    Chunk* cachedChunk = nullptr;
    auto resolve = [&](int wx, int wy, int wz) -> std::pair<Chunk*, size_t> {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return {nullptr, 0};
        int cx = worldToChunk(wx), cz = worldToChunk(wz);
        if (cx != cachedCX || cz != cachedCZ) {
            cachedCX = cx; cachedCZ = cz;
            cachedChunk = cm->getChunk(cx, cz);
        }
        if (!cachedChunk || !cachedChunk->skyLight) return {nullptr, 0};
        return {cachedChunk, toIdx(worldToLocal(wx, cx), wy, worldToLocal(wz, cz))};
    };

    struct Node { int x, y, z; };
    std::vector<Node> queue;
    queue.reserve(4096);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    uint8_t* sl = srcChunk->skyLight.get();
    sl[srcIdx] = (sl[srcIdx] & 0xF0) | (emission & 0xF);
    srcChunk->markDirty();
    queue.push_back({sx, sy, sz});

    size_t head = 0;
    while (head < queue.size()) {
        auto [bx, by, bz] = queue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        uint8_t light = unpackBlock(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            block_type bt = nc->blocks.get()[ni].getType();
            if (hasFlag(bt, BF_OPAQUE) && getBlockLightEmission(bt) == 0) continue;
            uint8_t propagated = light - 1;
            uint8_t* nlt = nc->skyLight.get();
            if (unpackBlock(nlt[ni]) >= propagated) continue;
            nlt[ni] = (nlt[ni] & 0xF0) | (propagated & 0xF);
            nc->markDirty();
            queue.push_back({nx, ny, nz});
        }
    }
}

void World::placeBlock(glm::ivec3 position, block_type type) const {
    int bx = position.x;
    int by = position.y;
    int bz = position.z;
    if (by < 0 || by >= CHUNK_HEIGHT) return;

    int chunkX = worldToChunk(bx);
    int chunkZ = worldToChunk(bz);
    int lx = worldToLocal(bx, chunkX);
    int lz = worldToLocal(bz, chunkZ);

    auto chunk = this->chunkManager->getChunk(chunkX, chunkZ);
    if (!chunk) return;
    chunk->placeBlock(lx, by, lz, type);
    markBorderNeighborsDirty(chunkManager, chunkX, chunkZ, lx, lz);

    // Flood block light in world space, crossing chunk boundaries
    uint8_t emission = getBlockLightEmission(type);
    if (emission > 0) {
        floodBlockLight(chunkManager, bx, by, bz, emission);
    }
}

World::~World() {
    delete this->terrainGenerator;
    delete this->chunkManager;
}

Chunk* World::getChunk(int x, int y) {
    return this->chunkManager->getChunk(x, y);
}

Cube* World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return nullptr;

    int chunkX = worldToChunk(x);
    int chunkZ = worldToChunk(z);

    auto res = this->chunkManager->getChunk(chunkX, chunkZ);
    if (res == nullptr) return nullptr;

    return res->getBlock(x - chunkX * CHUNK_SIZE, y, z - chunkZ * CHUNK_SIZE);
}

int World::render(const Shader& shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const {
    g_frame.meshBuildBudget = 4; // reset per frame
    auto planes = extractFrustumPlanes(viewProjection);

    // Collect visible chunks and sort front-to-back for early-Z rejection
    struct VisibleChunk {
        glm::ivec2 pos;
        Chunk* chunk;
        float distSq;
    };
    std::vector<VisibleChunk> visible;
    visible.reserve(chunkManager->chunks.size());

    // Fog fully hides chunks beyond this distance — skip rendering them
    float fogEnd = (float)(chunkManager->getRenderDistance() * CHUNK_SIZE);
    float maxRenderDistSq = fogEnd * fogEnd;

    for (auto& [pos, chunk] : chunkManager->chunks) {
        glm::vec3 minP(pos.x * CHUNK_SIZE, 0, pos.y * CHUNK_SIZE);
        glm::vec3 maxP(minP.x + CHUNK_SIZE, CHUNK_HEIGHT, minP.z + CHUNK_SIZE);
        if (!aabbInFrustum(planes, minP, maxP)) continue;
        glm::vec3 center(minP.x + CHUNK_SIZE * 0.5f, CHUNK_HEIGHT * 0.5f, minP.z + CHUNK_SIZE * 0.5f);
        float dx = center.x - cameraPos.x, dz = center.z - cameraPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > maxRenderDistSq) continue; // fully fogged, skip
        visible.push_back({pos, const_cast<Chunk*>(&chunk), distSq});
    }

    std::sort(visible.begin(), visible.end(),
              [](const VisibleChunk& a, const VisibleChunk& b) { return a.distSq < b.distSq; });

    // Pass 1: opaque geometry (front-to-back)
    shaderProgram.setInt("materialType", 0);
    int rendered = 0;
    for (auto& vc : visible) {
        Chunk* nx_neg = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y);
        Chunk* nx_pos = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y);
        Chunk* nz_neg = chunkManager->getChunk(vc.pos.x, vc.pos.y - 1);
        Chunk* nz_pos = chunkManager->getChunk(vc.pos.x, vc.pos.y + 1);
        vc.chunk->render(shaderProgram, nx_neg, nx_pos, nz_neg, nz_pos);
        rendered++;
        g_frame.chunksRendered++;
    }

    // Pass 2: transparent geometry (water + leaves) back-to-front
    shaderProgram.setInt("materialType", 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = (int)visible.size() - 1; i >= 0; i--) {
        auto& vc = visible[i];
        Chunk* nx_neg = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y);
        Chunk* nx_pos = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y);
        Chunk* nz_neg = chunkManager->getChunk(vc.pos.x, vc.pos.y - 1);
        Chunk* nz_pos = chunkManager->getChunk(vc.pos.x, vc.pos.y + 1);
        vc.chunk->renderWater(shaderProgram, nx_neg, nx_pos, nz_neg, nz_pos);
    }
    glDisable(GL_BLEND);

    return rendered;
}

void World::update(glm::vec3 cameraPosition) const {
    this->chunkManager->update(cameraPosition);
}

bool World::raycast(glm::vec3 origin, glm::vec3 direction, float maxDist, glm::ivec3& hitPos,
                    glm::ivec3& prevPos) const {
    // DDA voxel traversal (blocks are centered on integers: block n spans n-0.5 to n+0.5)
    direction = glm::normalize(direction);
    glm::vec3 shifted = origin + glm::vec3(0.5f);
    glm::ivec3 pos = glm::ivec3(std::floor(shifted.x), std::floor(shifted.y), std::floor(shifted.z));
    glm::ivec3 prev = pos;
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;

    for (int i = 0; i < 3; i++) {
        if (direction[i] > 0) {
            step[i] = 1;
            tMax[i] = ((pos[i] + 1) - shifted[i]) / direction[i];
        } else if (direction[i] < 0) {
            step[i] = -1;
            tMax[i] = (pos[i] - shifted[i]) / direction[i];
        } else {
            step[i] = 0;
            tMax[i] = 1e30f;
        }
        tDelta[i] = (direction[i] != 0) ? std::abs(1.0f / direction[i]) : 1e30f;
    }

    float dist = 0;
    while (dist < maxDist) {
        // Check current block
        if (pos.y >= 0 && pos.y < CHUNK_HEIGHT) {
            int chunkX = worldToChunk(pos.x);
            int chunkZ = worldToChunk(pos.z);
            Chunk* chunk = chunkManager->getChunk(chunkX, chunkZ);
            if (chunk) {
                int lx = worldToLocal(pos.x, chunkX);
                int lz = worldToLocal(pos.z, chunkZ);
                Cube* block = chunk->getBlock(lx, pos.y, lz);
                if (block && block->getType() != AIR && block->getType() != WATER) {
                    hitPos = pos;
                    prevPos = prev;
                    return true;
                }
            }
        }

        // Step to next voxel boundary
        prev = pos;
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            dist = tMax.x;
            tMax.x += tDelta.x;
            pos.x += step.x;
        } else if (tMax.y < tMax.z) {
            dist = tMax.y;
            tMax.y += tDelta.y;
            pos.y += step.y;
        } else {
            dist = tMax.z;
            tMax.z += tDelta.z;
            pos.z += step.z;
        }
    }
    return false;
}
