#include "world.h"
#include "entity_manager.h"
#include "frustum.h"
#include "lava_effects.h"
#include "light_data.h"
#include "light_propagation.h"
#include "mob_manager.h"
#include "particle_system.h"
#include "profiler.h"
#include "tnt_entity.h"
#include "tracy_shim.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <random>

World::World(unsigned int seed) : seed(seed) {
    this->terrainGenerator = new TerrainGenerator(seed, 0.1, 0, CHUNK_HEIGHT);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
    this->waterSimulator = new WaterSimulator(this);
    this->entityManager = new EntityManager();
    this->particles = new ParticleSystem();
    this->lavaEffects = new LavaEffects();
    this->mobManager = new MobManager(this, seed);
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
    block_type oldType = chunk->getBlockType(lx, y, lz);
    if (hasFlag(type, BF_OPAQUE) && !hasFlag(oldType, BF_OPAQUE)) {
        // Liquid hardening into stone / cobblestone / obsidian: placeBlock
        // also shades the sky light below, as a placed block would.
        chunk->placeBlock(lx, y, lz, type);
    } else {
        chunk->setBlockType(lx, y, lz, type);
        chunk->setWaterLevel(lx, y, lz, waterLevel);
    }
    markBorderNeighborsDirty(chunkManager, cx, cz, lx, y, lz);
    // Lava that flows in, drains away or hardens adds / takes its light.
    if (oldType == type) return;
    if (lightBatching)
        pendingLight.push_back({x, y, z, oldType, type});
    else
        relightBlockChange(chunkManager, x, y, z, oldType, type);
}

void World::endLightBatch() const {
    lightBatching = false;
    if (pendingLight.empty()) return;
    relightBlockChanges(chunkManager, pendingLight);
    pendingLight.clear();
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
    // Placing goes into air or, since aiming passes through liquids, into
    // water or lava: whatever was there takes its light with it.
    block_type oldType = chunk->getBlockType(lx, by, lz);
    chunk->placeBlock(lx, by, lz, type);
    markBorderNeighborsDirty(chunkManager, chunkX, chunkZ, lx, by, lz);

    // Block light in world space, crossing chunk boundaries
    relightBlockChange(chunkManager, bx, by, bz, oldType, type);

    // Wake the water simulator so placed water sources spread, and so that
    // any adjacent water reacts to a newly-placed solid block (it may now
    // be blocked off from flowing). destroyBlock has the symmetric call.
    if (hasFlag(type, BF_LIQUID)) {
        waterSimulator->activate(bx, by, bz);
    }
    waterSimulator->activateNeighbors(bx, by, bz);
}

World::~World() {
    delete this->mobManager;
    delete this->lavaEffects;
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

bool World::sectionVisible(glm::ivec3 block) const {
    if (!caveCullValid || block.y < 0 || block.y >= CHUNK_HEIGHT) return true;
    const Chunk* c = chunkManager->getChunk(worldToChunk(block.x), worldToChunk(block.z));
    return c && (c->visibleSections & (1u << (block.y >> 4))) != 0;
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

    // Cave culling: BFS from the camera's section through the per-section
    // air-connectivity graph; only reached sections emit draws. Runs over
    // loaded chunks (not just frustum survivors) because sight lines can
    // re-enter the frustum through chunks behind the camera.
    caveCullValid = false;
    if (g_caveCulling) {
        int camCx = worldToChunk((int)std::floor(cameraPos.x + 0.5f));
        int camCz = worldToChunk((int)std::floor(cameraPos.z + 0.5f));
        int camSy = std::clamp((int)std::floor(cameraPos.y) >> 4, 0, secvis::NUM_SECTIONS - 1);
        if (chunkManager->getChunk(camCx, camCz) == nullptr) {
            // Camera chunk not loaded (teleport/startup): degrade to no culling.
            for (auto& vc : visible) vc.chunk->visibleSections = 0xFF;
        } else {
            // Every loaded chunk's mask is cleared (not just the frustum
            // survivors') so sectionVisible() is exact for mobs too.
            caveCullValid = true;
            // Flat chunk-pointer grid: one hash pass over the chunk map up
            // front, then the BFS's per-section lookups are array reads
            // (it would otherwise do two map lookups per visited section).
            const int radius = chunkManager->getRenderDistance() + 1;
            const int side = 2 * radius + 1;
            static std::vector<Chunk*> grid;
            grid.assign(static_cast<size_t>(side) * side, nullptr);
            for (auto& [pos, chunk] : chunkManager->chunks) {
                const_cast<Chunk&>(chunk).visibleSections = 0;
                int dx = pos.x - camCx, dz = pos.y - camCz;
                if (dx < -radius || dx > radius || dz < -radius || dz > radius) continue;
                grid[static_cast<size_t>(dx + radius) * side + (dz + radius)] = const_cast<Chunk*>(&chunk);
            }
            auto cellAt = [&](int cx, int cz) -> Chunk* {
                int dx = cx - camCx, dz = cz - camCz;
                if (dx < -radius || dx > radius || dz < -radius || dz > radius) return nullptr;
                return grid[static_cast<size_t>(dx + radius) * side + (dz + radius)];
            };
            secvis::caveCullBFS(
                camCx, camCz, camSy, radius,
                [&](int cx, int cz, int sy, bool& loaded) -> uint16_t {
                    Chunk* c = cellAt(cx, cz);
                    if (!c) {
                        loaded = false;
                        return 0;
                    }
                    g_frame.caveBfsVisited++;
                    return c->sectionConn[sy];
                },
                [&](int cx, int cz, int sy) {
                    Chunk* c = cellAt(cx, cz);
                    if (c) c->visibleSections |= (uint8_t)(1u << sy);
                });
        }
    } else {
        for (auto& vc : visible) vc.chunk->visibleSections = 0xFF;
    }
    for (auto& vc : visible) {
        uint8_t vs = vc.chunk->visibleSections;
        for (int s = 0; s < secvis::NUM_SECTIONS; s++) {
            if (vs & (1u << s))
                g_frame.sectionsRendered++;
            else
                g_frame.sectionsCulled++;
        }
    }

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
    {
        ZoneScopedN("Water::tick");
        this->waterSimulator->tick();
    }
    {
        ZoneScopedN("Water::updateAmbient");
        this->waterSimulator->updateAmbient(cameraPosition);
    }
    {
        ZoneScopedN("Lava::effects");
        this->lavaEffects->update(*this, cameraPosition, dt);
    }
    {
        ZoneScopedN("Entities::tick");
        this->entityManager->tick(const_cast<World*>(this), dt, now);
    }
    {
        ZoneScopedN("Mobs::tick");
        this->mobManager->update(dt, now);
    }
    {
        ZoneScopedN("Particles::update");
        this->particles->update(dt);
    }
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
                if (hasFlag(t, BF_LIQUID)) continue; // don't consume water or lava
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

    // Entity damage + knockback (mobs and, via its hook, the player).
    mobManager->applyExplosion(center, power);

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
                if (block && block->getType() != AIR && !hasFlag(block->getType(), BF_LIQUID)) {
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
