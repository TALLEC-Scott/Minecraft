//
// Created by scott on 03/07/23.
//

#include "ChunkManager.h"
#include <cmath>
#include "world.h"
#include "tracy_shim.h"
#include "profiler.h"
#include <initializer_list>
#include <algorithm>

#ifdef __EMSCRIPTEN__
static constexpr int MAX_CHUNKS_PER_FRAME = 4;
#else
static constexpr int MAX_INTEGRATE_PER_FRAME = 2;
static constexpr int MAX_MESH_RESULTS_PER_FRAME = 8;
#endif

ChunkManager::ChunkManager(int renderDist, int /*chunkSize*/, TerrainGenerator& terrainGenerator)
    : renderDistance(renderDist), terrainGenerator(terrainGenerator) {
    int diameter = 2 * renderDist + 1;
    chunks.reserve(static_cast<size_t>(diameter) * diameter * 2);

#ifndef __EMSCRIPTEN__
    int numWorkers = std::min(4u, std::max(1u, std::thread::hardware_concurrency() - 1));
    for (int i = 0; i < numWorkers; i++) workers.emplace_back(&ChunkManager::workerLoop, this);
#endif
}

ChunkManager::~ChunkManager() {
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        shutdownFlag = true;
    }
    requestCV.notify_all();
    for (auto& w : workers) w.join();
#endif
}

size_t ChunkManager::totalChunkMemory() const {
    size_t total = 0;
    for (const auto& [pos, chunk] : chunks) total += chunk.memoryUsage();
    return total;
}

Chunk::MemBreakdown ChunkManager::totalChunkBreakdown() const {
    Chunk::MemBreakdown tot;
    for (const auto& [pos, chunk] : chunks) {
        Chunk::MemBreakdown mb = chunk.memoryBreakdown();
        tot.sections += mb.sections;
        tot.skyLight += mb.skyLight;
        tot.waterLevels += mb.waterLevels;
        tot.meshCache += mb.meshCache;
        tot.pendingMesh += mb.pendingMesh;
    }
    return tot;
}

void ChunkManager::update(glm::vec3 cameraPosition) {
    ZoneScopedN("ChunkManager::update");
    int cx = (int)std::floor(cameraPosition.x / CHUNK_SIZE);
    int cz = (int)std::floor(cameraPosition.z / CHUNK_SIZE);
    glm::ivec2 currentChunk = glm::ivec2(cx, cz);

    glm::ivec2 minChunk = currentChunk - glm::ivec2(renderDistance);
    glm::ivec2 maxChunk = currentChunk + glm::ivec2(renderDistance);

#ifndef __EMSCRIPTEN__
    drainResults();
    // Native path: workers generate chunks asynchronously, so loadChunks
    // just queues requests. Skip the N² box scan when the player hasn't
    // crossed a chunk boundary - workers drain the existing queue on
    // their own. In steady state this dominates the update budget.
    bool boxChanged = (minChunk != currentMin) || (maxChunk != currentMax);
    currentMin = minChunk;
    currentMax = maxChunk;
    if (boxChanged) {
        loadChunks(minChunk, maxChunk);
        unloadChunks(minChunk, maxChunk);
    }
#else
    // Web: loadChunks generates MAX_CHUNKS_PER_FRAME chunks synchronously
    // each call. We MUST call it every frame or the world stops streaming
    // after the first render-distance-box worth of chunks. unloadChunks
    // can still early-out since nothing's moved.
    currentMin = minChunk;
    currentMax = maxChunk;
    loadChunks(minChunk, maxChunk);
    unloadChunks(minChunk, maxChunk);
#endif

    // Flush pendingMesh for chunks that haven't been rendered in a while.
    // Without this, async mesh builds for invisible chunks leak the full
    // combined mesh data (hundreds of MB across the world).
    // 20 frames (~333ms at 60fps) matches common voxel-engine practice —
    // short enough to reclaim memory, long enough to absorb camera spins
    // and rapid-rebuild transients without redundant GPU uploads.
    constexpr int STALE_FRAMES = 20;
    constexpr int FLUSH_BUDGET = 16; // cap per-frame uploads to avoid stalls
    int flushed = 0;
    for (auto& [pos, chunk] : chunks) {
        chunk.framesSinceRender++;
        if (chunk.hasPendingMesh() && chunk.framesSinceRender > STALE_FRAMES && flushed < FLUSH_BUDGET) {
            chunk.uploadMesh();
            flushed++;
        }
    }

    // Sample memory once per second (totalChunkMemory iterates all chunks,
    // not cheap enough for per-frame).
    static double lastMemSample = 0.0;
    double now = glfwGetTime();
    if (now - lastMemSample >= 1.0) {
        Chunk::MemBreakdown mb = totalChunkBreakdown();
        size_t chunkMem = mb.sections + mb.skyLight + mb.waterLevels + mb.meshCache + mb.pendingMesh;
        size_t rss = sampleProcessRss();
        TracyPlot("chunks.loaded", (int64_t)chunks.size());
        TracyPlot("chunks.memory_bytes", (int64_t)chunkMem);
        TracyPlot("mem.sections_bytes", (int64_t)mb.sections);
        TracyPlot("mem.skylight_bytes", (int64_t)mb.skyLight);
        TracyPlot("mem.waterlevels_bytes", (int64_t)mb.waterLevels);
        TracyPlot("mem.meshcache_bytes", (int64_t)mb.meshCache);
        TracyPlot("process.rss_bytes", (int64_t)rss);
        g_frame.chunkMemoryBytes = chunkMem;
        g_frame.memSectionsBytes = mb.sections;
        g_frame.memSkyLightBytes = mb.skyLight;
        g_frame.memWaterLevelsBytes = mb.waterLevels;
        g_frame.memMeshCacheBytes = mb.meshCache;
        g_frame.memPendingMeshBytes = mb.pendingMesh;
        g_frame.processRssBytes = rss;
        lastMemSample = now;
    }
}

void ChunkManager::loadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
#ifdef __EMSCRIPTEN__
    int generated = 0;
    for (int x = minChunk.x; x <= maxChunk.x && generated < MAX_CHUNKS_PER_FRAME; x++) {
        for (int z = minChunk.y; z <= maxChunk.y && generated < MAX_CHUNKS_PER_FRAME; z++) {
            if (chunks.find(glm::ivec2(x, z)) == chunks.end()) {
                // Try loading from disk first
                if (worldSave && worldSave->chunkExists(x, z)) {
                    ChunkData data;
                    if (worldSave->loadChunkData(x, z, data, terrainGenerator)) {
                        chunks[glm::ivec2(x, z)] = Chunk(std::move(data));
                        Chunk& c = chunks[glm::ivec2(x, z)];
                        c.propagateBorderLight(getChunk(x - 1, z), getChunk(x + 1, z), getChunk(x, z - 1),
                                               getChunk(x, z + 1));
                        for (auto& [dx, dz] :
                             std::initializer_list<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                            auto it2 = chunks.find(glm::ivec2(x + dx, z + dz));
                            if (it2 != chunks.end()) it2->second.markDirty();
                        }
                        generated++;
                        continue;
                    }
                }
                generateChunk(x, z);
                generated++;
            }
        }
    }
#else
    for (int x = minChunk.x; x <= maxChunk.x; x++) {
        for (int z = minChunk.y; z <= maxChunk.y; z++) {
            glm::ivec2 pos(x, z);
            if (chunks.find(pos) == chunks.end() && pendingChunks.find(pos) == pendingChunks.end()) {
                pendingChunks.insert(pos);
                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    requestQueue.push(pos);
                }
                requestCV.notify_one();
            }
        }
    }
#endif
}

void ChunkManager::saveAllModifiedChunks() {
    if (!worldSave) return;
    for (auto& [pos, chunk] : chunks) {
        if (chunk.modified) {
            worldSave->saveChunk(chunk);
            chunk.modified = false;
        }
    }
}

void ChunkManager::unloadChunks(glm::ivec2 minChunk, glm::ivec2 maxChunk) {
    for (auto it = chunks.begin(); it != chunks.end();) {
        if (it->first.x < minChunk.x || it->first.x > maxChunk.x || it->first.y < minChunk.y ||
            it->first.y > maxChunk.y) {
            if (worldSave && it->second.modified) {
                worldSave->saveChunk(it->second);
            }
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }

#ifndef __EMSCRIPTEN__
    for (auto it = pendingChunks.begin(); it != pendingChunks.end();) {
        if (it->x < minChunk.x || it->x > maxChunk.x || it->y < minChunk.y || it->y > maxChunk.y)
            it = pendingChunks.erase(it);
        else
            ++it;
    }
#endif
}

void ChunkManager::generateChunk(int x, int z) {
    Chunk chunk(x, z, terrainGenerator);
    chunks[glm::ivec2(x, z)] = std::move(chunk);
    // Cross-chunk light propagation
    Chunk& newChunk = chunks[glm::ivec2(x, z)];
    newChunk.propagateBorderLight(getChunk(x - 1, z), getChunk(x + 1, z), getChunk(x, z - 1), getChunk(x, z + 1));
    for (auto& [dx, dz] : std::initializer_list<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
        auto it = chunks.find(glm::ivec2(x + dx, z + dz));
        if (it != chunks.end()) it->second.markDirty();
    }
}

Chunk* ChunkManager::getChunk(int chunkX, int chunkZ) {
    glm::ivec2 chunkPos = glm::ivec2(chunkX, chunkZ);
    auto it = chunks.find(chunkPos);
    if (it != chunks.end()) {
        return &(it->second);
    }
    return nullptr;
}

#ifndef __EMSCRIPTEN__
void ChunkManager::workerLoop() {
    while (true) {
        bool isChunkGen = false;
        bool isMeshBuild = false;
        glm::ivec2 pos;
        MeshRequest meshReq;
        {
            std::unique_lock<std::mutex> lock(requestMutex);
            requestCV.wait(lock,
                           [&] { return !requestQueue.empty() || !meshRequestQueue.empty() || shutdownFlag.load(); });
            if (shutdownFlag.load() && requestQueue.empty() && meshRequestQueue.empty()) return;

            if (!meshRequestQueue.empty()) {
                meshReq = std::move(meshRequestQueue.front());
                meshRequestQueue.pop();
                isMeshBuild = true;
            } else if (!requestQueue.empty()) {
                pos = requestQueue.front();
                requestQueue.pop();
                isChunkGen = true;
            }
        }

        try {
            if (isChunkGen) {
                ChunkData data;
                bool loaded = false;
                if (worldSave && worldSave->chunkExists(pos.x, pos.y)) {
                    loaded = worldSave->loadChunkData(pos.x, pos.y, data, terrainGenerator);
                }
                if (!loaded) {
                    data = generateChunkData(pos.x, pos.y, terrainGenerator);
                }
                std::lock_guard<std::mutex> lock(resultMutex);
                resultQueue.push(std::move(data));
            } else if (isMeshBuild) {
                Chunk::MeshData mesh =
                    buildMeshFromData(meshReq.blocks.get(), meshReq.skyLight.get(), meshReq.waterLevels.get(),
                                      meshReq.maxSolidY, meshReq.chunkX, meshReq.chunkZ, meshReq.borders);
                std::lock_guard<std::mutex> lock(resultMutex);
                meshResultQueue.push({meshReq.pos, std::move(mesh)});
            }
        } catch (const std::exception& e) {
            printf("Worker exception: %s\n", e.what());
        } catch (...) {
            printf("Worker unknown exception\n");
        }
    }
}

void ChunkManager::queueMeshBuild(glm::ivec2 pos) {
    auto it = chunks.find(pos);
    if (it == chunks.end()) return;
    Chunk& chunk = it->second;
    if (chunk.meshBuildInFlight) return;

    // Snapshot neighbor borders (including diagonals for 4-chunk corner averaging)
    Chunk::NeighborChunks nc;
    nc.nxNeg = getChunk(pos.x - 1, pos.y);
    nc.nxPos = getChunk(pos.x + 1, pos.y);
    nc.nzNeg = getChunk(pos.x, pos.y - 1);
    nc.nzPos = getChunk(pos.x, pos.y + 1);
    nc.dNN = getChunk(pos.x - 1, pos.y - 1);
    nc.dNP = getChunk(pos.x - 1, pos.y + 1);
    nc.dPN = getChunk(pos.x + 1, pos.y - 1);
    nc.dPP = getChunk(pos.x + 1, pos.y + 1);
    Chunk::NeighborBorders borders = Chunk::snapshotBorders(nc);

    MeshRequest req;
    req.pos = pos;
    req.blocks = chunk.decompressBlocks();
    chunk.ensureSkyLightFlat();          // async worker needs the flat array
    req.skyLight = chunk.skyLight;       // shared_ptr copy; worker holds ref
    req.waterLevels = chunk.waterLevels; // shared_ptr copy of per-block flow levels
    req.maxSolidY = chunk.maxSolidY;
    req.chunkX = chunk.chunkX;
    req.chunkZ = chunk.chunkY; // chunkY is actually Z coordinate
    req.borders = borders;

    // Snapshot which dirty bits this build covers, so uploadMesh only
    // clears THOSE bits — not bits set by the water sim after the snapshot.
    chunk.builtDirtyMask = chunk.sectionDirty;
    chunk.meshBuildInFlight = true;

    {
        std::lock_guard<std::mutex> lock(requestMutex);
        meshRequestQueue.push(std::move(req));
    }
    requestCV.notify_one();
}

void ChunkManager::queueDirtyMeshBuilds() {
    // Queue mesh builds for dirty chunks that aren't already in-flight.
    // Don't re-dirty after queueing — the dirty bits are preserved until
    // uploadMesh clears them. New edits during the in-flight build set
    // their own section bits, which survive the upload and trigger
    // another rebuild next frame.
    for (auto& [pos, chunk] : chunks) {
        if (chunk.isMeshDirty() && !chunk.meshBuildInFlight) {
            // Chunks that have been sync-rebuilt (sectionCachesPopulated)
            // use the sync path in render() for incremental per-section
            // updates. Don't re-queue them async — the async path builds
            // the full chunk from a snapshot and would overwrite the
            // carefully-maintained section caches with stale data.
            if (chunk.sectionCachesPopulated) continue;
            queueMeshBuild(pos);
        }
    }
}

void ChunkManager::drainResults() {
    // Drain completed chunk generations
    int integrated = 0;
    while (integrated < MAX_INTEGRATE_PER_FRAME) {
        ChunkData data;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (resultQueue.empty()) break;
            data = std::move(resultQueue.front());
            resultQueue.pop();
        }

        glm::ivec2 pos(data.chunkX, data.chunkZ);
        pendingChunks.erase(pos);

        if (pos.x < currentMin.x || pos.x > currentMax.x || pos.y < currentMin.y || pos.y > currentMax.y) {
            continue;
        }

        chunks[pos] = Chunk(std::move(data));

        // Cross-chunk light propagation
        chunks[pos].propagateBorderLight(getChunk(pos.x - 1, pos.y), getChunk(pos.x + 1, pos.y),
                                         getChunk(pos.x, pos.y - 1), getChunk(pos.x, pos.y + 1));

        // Queue async mesh build for the new chunk and its neighbors
        queueMeshBuild(pos);
        for (auto& [dx, dz] : std::initializer_list<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            glm::ivec2 npos(pos.x + dx, pos.y + dz);
            auto it = chunks.find(npos);
            if (it != chunks.end()) {
                it->second.markDirty();
                queueMeshBuild(npos);
            }
        }

        integrated++;
    }

    // Drain completed mesh builds
    int meshDrained = 0;
    while (meshDrained < MAX_MESH_RESULTS_PER_FRAME) {
        MeshResult res;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (meshResultQueue.empty()) break;
            res = std::move(meshResultQueue.front());
            meshResultQueue.pop();
        }

        auto it = chunks.find(res.pos);
        if (it == chunks.end()) continue; // chunk was unloaded

        it->second.setPendingMesh(std::move(res.mesh));
        meshDrained++;
    }
}
#endif
