#include "world.h"
#include "entity_manager.h"
#include "light_data.h"
#include "light_propagation.h"
#include "particle_system.h"
#include "profiler.h"
#include "tnt_entity.h"
#include "tracy_shim.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <random>

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

World::World(unsigned int seed) : seed(seed) {
    this->terrainGenerator = new TerrainGenerator(seed, 0.1, 0, CHUNK_HEIGHT);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
    this->waterSimulator = new WaterSimulator(this);
    this->entityManager = new EntityManager();
    this->particles = new ParticleSystem();
}

static void markBorderNeighborsDirty(ChunkManager* cm, int chunkX, int chunkZ, int lx, int y, int lz) {
    // When an edge block changes, direct X/Z neighbor's corner averaging
    // reads into this block. Diagonal-corner blocks change affects the
    // diagonal chunk too (4-chunk intersection at corner vertices).
    int sy = y / 16;
    auto mark = [&](int dx, int dz) {
        Chunk* n = cm->getChunk(chunkX + dx, chunkZ + dz);
        if (n) n->markSectionDirty(sy);
    };
    bool onXNeg = (lx == 0), onXPos = (lx == CHUNK_SIZE - 1);
    bool onZNeg = (lz == 0), onZPos = (lz == CHUNK_SIZE - 1);
    if (onXNeg) mark(-1, 0);
    if (onXPos) mark(1, 0);
    if (onZNeg) mark(0, -1);
    if (onZPos) mark(0, 1);
    // Diagonal neighbors at 4-chunk corner intersections
    if (onXNeg && onZNeg) mark(-1, -1);
    if (onXNeg && onZPos) mark(-1, 1);
    if (onXPos && onZNeg) mark(1, -1);
    if (onXPos && onZPos) mark(1, 1);
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
    if (getBlockLightEmission(oldType) > 0) {
        removeBlockLightWorld(chunkManager, bx, by, bz);
    } else {
        // Non-emissive block removed: the now-exposed cell may need block
        // light from an adjacent emitter (e.g. glowstone next door). Seed
        // a BFS from the max of the 6 neighbor block-light values minus 1.
        WorldResolver resolve(chunkManager);
        uint8_t maxNeighbor = 0;
        for (auto& d : DIRS_6) {
            auto [nc, ni] = resolve(bx + d[0], by + d[1], bz + d[2]);
            if (!nc) continue;
            nc->ensureSkyLightFlat();
            uint8_t nl = unpackBlock(nc->skyLight.get()[ni]);
            if (nl > maxNeighbor) maxNeighbor = nl;
        }
        if (maxNeighbor > 0) {
            floodBlockLight(chunkManager, bx, by, bz, maxNeighbor - 1);
        }
    }

    // Trigger water simulation for neighbors (water may flow into the gap)
    waterSimulator->activateNeighbors(bx, by, bz);
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
    delete this->particles;
    delete this->entityManager;
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
        Chunk::NeighborChunks nc;
        nc.nxNeg = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y);
        nc.nxPos = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y);
        nc.nzNeg = chunkManager->getChunk(vc.pos.x, vc.pos.y - 1);
        nc.nzPos = chunkManager->getChunk(vc.pos.x, vc.pos.y + 1);
        nc.dNN = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y - 1);
        nc.dNP = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y + 1);
        nc.dPN = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y - 1);
        nc.dPP = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y + 1);
        vc.chunk->render(shaderProgram, nc);
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

void World::update(glm::vec3 cameraPosition, float dt, double now) const {
    ZoneScopedN("World::update");
    lastCameraPos = cameraPosition;
    this->chunkManager->update(cameraPosition);
    this->waterSimulator->tick();
    this->waterSimulator->updateAmbient(cameraPosition);
    this->entityManager->tick(const_cast<World*>(this), dt, now);
    this->particles->update(dt);
    // Decay camera shake toward zero so a single explode() call produces a
    // finite rattle rather than a permanent one.
    if (cameraShake > 0.0f) {
        cameraShake -= dt * 1.8f;
        if (cameraShake < 0.0f) cameraShake = 0.0f;
    }
}

void World::igniteTnt(glm::ivec3 pos, double now) const {
    Cube* b = getBlock(pos.x, pos.y, pos.z);
    if (!b || b->getType() != TNT) return;
    setBlock(pos.x, pos.y, pos.z, AIR);
    floodSkyLightWorld(chunkManager, pos.x, pos.y, pos.z);
    waterSimulator->activateNeighbors(pos.x, pos.y, pos.z);
    // Spawn entity at block center so it sits flush with the world grid.
    entityManager->spawnTnt(glm::vec3(pos.x, pos.y, pos.z), TntEntity::DEFAULT_FUSE, now);
}

void World::explode(glm::vec3 center, float power, double now) const {
    static std::mt19937 rng(0xD06F00Du);
    std::uniform_int_distribution<int> chainFuse(TntEntity::CHAIN_FUSE_MIN, TntEntity::CHAIN_FUSE_MAX);

    int ir = static_cast<int>(std::ceil(power));
    int cx = static_cast<int>(std::floor(center.x + 0.5f));
    int cy = static_cast<int>(std::floor(center.y + 0.5f));
    int cz = static_cast<int>(std::floor(center.z + 0.5f));

    for (int dx = -ir; dx <= ir; ++dx) {
        for (int dy = -ir; dy <= ir; ++dy) {
            for (int dz = -ir; dz <= ir; ++dz) {
                int bx = cx + dx;
                int by = cy + dy;
                int bz = cz + dz;
                if (by < 0 || by >= CHUNK_HEIGHT) continue;
                float d = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                if (d > power) continue;
                Cube* b = getBlock(bx, by, bz);
                if (!b) continue;
                block_type t = b->getType();
                if (t == AIR) continue;
                if (t == BEDROCK) continue; // indestructible
                if (t == WATER) continue;   // don't consume liquid
                if (t == TNT) {
                    // Chain reaction — replace with a short-fused primed entity.
                    // Mirror igniteTnt()'s full transition: light flood +
                    // water wake, otherwise the now-empty cell keeps its
                    // stale skyLight=0 byte and adjacent water doesn't learn
                    // it can flow into the gap until its next scheduled tick.
                    setBlock(bx, by, bz, AIR);
                    floodSkyLightWorld(chunkManager, bx, by, bz);
                    waterSimulator->activateNeighbors(bx, by, bz);
                    entityManager->spawnTnt(glm::vec3(bx, by, bz), chainFuse(rng), now);
                    continue;
                }
                destroyBlock(glm::vec3(bx, by, bz));
            }
        }
    }

    // Smoke plume + audio
    particles->spawnSmokePlume(center);
    entityManager->playExplosion(center);

    // Camera shake: closer = stronger. Max out at magnitude 0.6 when
    // standing on top of the charge.
    glm::vec3 toPlayer = lastCameraPos - center;
    float dist = glm::length(toPlayer);
    float shake = 0.6f / (1.0f + dist * 0.15f);
    if (shake > cameraShake) cameraShake = shake;
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
