#include "world.h"
#include "light_data.h"
#include "profiler.h"
#include "tracy_shim.h"
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
    this->waterSimulator = new WaterSimulator(this);
}

static void markBorderNeighborsDirty(ChunkManager* cm, int chunkX, int chunkZ, int lx, int y, int lz) {
    // Only neighbor chunks whose border faces the edited block need a
    // rebuild, and only the section at the same Y level.
    int sy = y / 16;
    if (lx == 0) {
        Chunk* n = cm->getChunk(chunkX - 1, chunkZ);
        if (n) n->markSectionDirty(sy);
    }
    if (lx == CHUNK_SIZE - 1) {
        Chunk* n = cm->getChunk(chunkX + 1, chunkZ);
        if (n) n->markSectionDirty(sy);
    }
    if (lz == 0) {
        Chunk* n = cm->getChunk(chunkX, chunkZ - 1);
        if (n) n->markSectionDirty(sy);
    }
    if (lz == CHUNK_SIZE - 1) {
        Chunk* n = cm->getChunk(chunkX, chunkZ + 1);
        if (n) n->markSectionDirty(sy);
    }
}

// World-space sky light BFS after block removal — crosses chunk boundaries.
static void floodSkyLightWorld(ChunkManager* cm, int sx, int sy, int sz) {
    WorldResolver resolve(cm);

    // Determine initial light from neighbors
    uint8_t maxLight = 0;
    for (auto& d : DIRS_6) {
        auto [nc, ni] = resolve(sx + d[0], sy + d[1], sz + d[2]);
        if (nc) {
            uint8_t nl = unpackSky(nc->skyLight.get()[ni]);
            if (nl > maxLight) maxLight = nl;
        }
    }
    if (sy + 1 >= CHUNK_HEIGHT) maxLight = 15;

    // Sky column rule
    auto [aboveChunk, aboveIdx] = resolve(sx, sy + 1, sz);
    uint8_t newLight = (aboveChunk && unpackSky(aboveChunk->skyLight.get()[aboveIdx]) == 15)
                           ? 15 : (maxLight > 0 ? maxLight - 1 : 0);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    srcChunk->skyLight.get()[srcIdx] = packLight(newLight, unpackBlock(srcChunk->skyLight.get()[srcIdx]));
    srcChunk->markSectionDirty(sy / 16);

    struct Node { int x, y, z; };
    std::vector<Node> queue;
    queue.reserve(256);
    queue.push_back({sx, sy, sz});

    size_t head = 0;
    while (head < queue.size()) {
        auto [bx, by, bz] = queue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        uint8_t light = unpackSky(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            block_type bt = nc->getBlockType(worldToLocal(nx, worldToChunk(nx)), ny, worldToLocal(nz, worldToChunk(nz)));
            if (hasFlag(bt, BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            if (unpackSky(nc->skyLight.get()[ni]) >= propagated) continue;
            nc->skyLight.get()[ni] = packLight(propagated, unpackBlock(nc->skyLight.get()[ni]));
            nc->markSectionDirty(ny / 16);
            queue.push_back({nx, ny, nz});
        }
    }
}

// World-space block light removal BFS — zeroes light from a destroyed emissive block,
// then re-propagates from any remaining light sources at the boundary.
static void removeBlockLightWorld(ChunkManager* cm, int sx, int sy, int sz) {
    WorldResolver resolve(cm);

    // Phase 1: removal BFS — zero out light that was from this source
    struct Node { int x, y, z; uint8_t oldLight; };
    std::vector<Node> removeQueue;
    removeQueue.reserve(4096);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    uint8_t srcLight = unpackBlock(srcChunk->skyLight.get()[srcIdx]);
    srcChunk->skyLight.get()[srcIdx] = packLight(unpackSky(srcChunk->skyLight.get()[srcIdx]), 0);
    srcChunk->markSectionDirty(sy / 16);
    removeQueue.push_back({sx, sy, sz, srcLight});

    struct LightNode { int x, y, z; };
    std::vector<LightNode> relightSeeds;

    size_t head = 0;
    while (head < removeQueue.size()) {
        auto [bx, by, bz, oldLight] = removeQueue[head++];
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            uint8_t neighborLight = unpackBlock(nc->skyLight.get()[ni]);
            if (neighborLight > 0 && neighborLight < oldLight) {
                nc->skyLight.get()[ni] = packLight(unpackSky(nc->skyLight.get()[ni]), 0);
                nc->markSectionDirty(ny / 16);
                removeQueue.push_back({nx, ny, nz, neighborLight});
            } else if (neighborLight >= oldLight && neighborLight > 0) {
                relightSeeds.push_back({nx, ny, nz});
            }
        }
    }

    std::vector<LightNode> lightQueue;
    for (auto& s : relightSeeds) lightQueue.push_back(s);
    head = 0;
    while (head < lightQueue.size()) {
        auto [bx, by, bz] = lightQueue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        uint8_t light = unpackBlock(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            int lx = worldToLocal(nx, worldToChunk(nx)), lz = worldToLocal(nz, worldToChunk(nz));
            block_type bt = nc->getBlockType(lx, ny, lz);
            if (hasFlag(bt, BF_OPAQUE) && getBlockLightEmission(bt) == 0) continue;
            uint8_t propagated = light - 1;
            if (unpackBlock(nc->skyLight.get()[ni]) >= propagated) continue;
            nc->skyLight.get()[ni] = packLight(unpackSky(nc->skyLight.get()[ni]), propagated);
            nc->markSectionDirty(ny / 16);
            lightQueue.push_back({nx, ny, nz});
        }
    }
}

void World::setBlock(int x, int y, int z, block_type type, uint8_t waterLevel) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return;

    int cx = worldToChunk(x);
    int cz = worldToChunk(z);
    int lx = worldToLocal(x, cx);
    int lz = worldToLocal(z, cz);

    Chunk* chunk = chunkManager->getChunk(cx, cz);
    if (!chunk) return;

    // setBlockType marks only the affected section (+ boundary neighbors)
    // dirty via markSectionDirty. Don't call markDirty() here — that would
    // set ALL sections dirty and defeat the incremental rebuild.
    chunk->setBlockType(lx, y, lz, type);
    chunk->setWaterLevel(lx, y, lz, waterLevel);
    markBorderNeighborsDirty(chunkManager, cx, cz, lx, y, lz);
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

    block_type oldType = chunk->getBlockType(lx, by, lz);
    chunk->destroyBlock(lx, by, lz);
    markBorderNeighborsDirty(chunkManager, chunkX, chunkZ, lx, by, lz);

    // World-space light updates — cross chunk boundaries
    floodSkyLightWorld(chunkManager, bx, by, bz);
    if (getBlockLightEmission(oldType) > 0)
        removeBlockLightWorld(chunkManager, bx, by, bz);

    // Trigger water simulation for neighbors (water may flow into the gap)
    waterSimulator->activateNeighbors(bx, by, bz);
}

// World-space BFS: flood block light from a source, crossing chunk boundaries.
// Caches chunk pointer to avoid repeated hash lookups for blocks in the same chunk.
static void floodBlockLight(ChunkManager* cm, int sx, int sy, int sz, uint8_t emission) {
    WorldResolver resolve(cm);

    struct Node { int x, y, z; };
    std::vector<Node> queue;
    queue.reserve(4096);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    uint8_t* sl = srcChunk->skyLight.get();
    sl[srcIdx] = (sl[srcIdx] & 0xF0) | (emission & 0xF);
    srcChunk->markSectionDirty(sy / 16);
    queue.push_back({sx, sy, sz});

    size_t head = 0;
    while (head < queue.size()) {
        auto [bx, by, bz] = queue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        uint8_t light = unpackBlock(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            int lx = worldToLocal(nx, worldToChunk(nx));
            int lz = worldToLocal(nz, worldToChunk(nz));
            block_type bt = nc->getBlockType(lx, ny, lz);
            if (hasFlag(bt, BF_OPAQUE) && getBlockLightEmission(bt) == 0) continue;
            uint8_t propagated = light - 1;
            uint8_t* nlt = nc->skyLight.get();
            if (unpackBlock(nlt[ni]) >= propagated) continue;
            nlt[ni] = (nlt[ni] & 0xF0) | (propagated & 0xF);
            nc->markSectionDirty(ny / 16);
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
    markBorderNeighborsDirty(chunkManager, chunkX, chunkZ, lx, by, lz);

    // Flood block light in world space, crossing chunk boundaries
    uint8_t emission = getBlockLightEmission(type);
    if (emission > 0) {
        floodBlockLight(chunkManager, bx, by, bz, emission);
    }

    // Wake the water simulator so placed water sources spread, and so that
    // any adjacent water reacts to a newly-placed solid block (it may now
    // be blocked off from flowing). destroyBlock has the symmetric call.
    if (type == WATER) {
        waterSimulator->activate(bx, by, bz);
    }
    waterSimulator->activateNeighbors(bx, by, bz);
}

World::~World() {
    delete this->waterSimulator;
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

    // Delegate to Chunk::getBlock (uses thread-local scratch)
    return res->getBlock(x - chunkX * CHUNK_SIZE, y, z - chunkZ * CHUNK_SIZE);
}

int World::render(const Shader& shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const {
    ZoneScopedN("World::render");
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

    // Pass 2: transparent geometry (water) back-to-front.
    // Double-sided so water edges are visible from both directions.
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
    ZoneScopedN("World::update");
    this->chunkManager->update(cameraPosition);
    this->waterSimulator->tick();
    this->waterSimulator->updateAmbient(cameraPosition);
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
