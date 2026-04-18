#include "light_propagation.h"

#include <vector>

#include "ChunkManager.h"
#include "chunk.h"
#include "cube.h"
#include "light_data.h"
#include "world_resolver.h"

using WorldResolver = WorldResolverT<ChunkManager, Chunk>;

void floodSkyLightWorld(ChunkManager* cm, int sx, int sy, int sz) {
    WorldResolver resolve(cm);

    uint8_t maxLight = 0;
    for (auto& d : DIRS_6) {
        auto [nc, ni] = resolve(sx + d[0], sy + d[1], sz + d[2]);
        if (nc) {
            nc->ensureSkyLightFlat();
            uint8_t nl = unpackSky(nc->skyLight.get()[ni]);
            if (nl > maxLight) maxLight = nl;
        }
    }
    if (sy + 1 >= CHUNK_HEIGHT) maxLight = 15;

    auto [aboveChunk, aboveIdx] = resolve(sx, sy + 1, sz);
    if (aboveChunk) aboveChunk->ensureSkyLightFlat();
    uint8_t newLight =
        (aboveChunk && unpackSky(aboveChunk->skyLight.get()[aboveIdx]) == 15) ? 15 : (maxLight > 0 ? maxLight - 1 : 0);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    srcChunk->ensureSkyLightFlat();
    srcChunk->skyLight.get()[srcIdx] = packLight(newLight, unpackBlock(srcChunk->skyLight.get()[srcIdx]));
    srcChunk->markSectionDirty(sy / 16);

    struct Node {
        int x, y, z;
    };
    std::vector<Node> queue;
    queue.reserve(256);
    queue.push_back({sx, sy, sz});

    size_t head = 0;
    while (head < queue.size()) {
        auto [bx, by, bz] = queue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        chunk->ensureSkyLightFlat();
        uint8_t light = unpackSky(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            nc->ensureSkyLightFlat();
            block_type bt =
                nc->getBlockType(worldToLocal(nx, worldToChunk(nx)), ny, worldToLocal(nz, worldToChunk(nz)));
            if (hasFlag(bt, BF_OPAQUE)) continue;
            uint8_t propagated = (light == 15 && d[1] == -1) ? 15 : (light - 1);
            if (unpackSky(nc->skyLight.get()[ni]) >= propagated) continue;
            nc->skyLight.get()[ni] = packLight(propagated, unpackBlock(nc->skyLight.get()[ni]));
            nc->markSectionDirty(ny / 16);
            queue.push_back({nx, ny, nz});
        }
    }
}

void floodBlockLight(ChunkManager* cm, int sx, int sy, int sz, uint8_t emission) {
    WorldResolver resolve(cm);

    struct Node {
        int x, y, z;
    };
    std::vector<Node> queue;
    queue.reserve(4096);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    srcChunk->ensureSkyLightFlat();
    uint8_t* sl = srcChunk->skyLight.get();
    sl[srcIdx] = (sl[srcIdx] & 0xF0) | (emission & 0xF);
    srcChunk->markSectionDirty(sy / 16);
    queue.push_back({sx, sy, sz});

    size_t head = 0;
    while (head < queue.size()) {
        auto [bx, by, bz] = queue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        chunk->ensureSkyLightFlat();
        uint8_t light = unpackBlock(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            nc->ensureSkyLightFlat();
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

void removeBlockLightWorld(ChunkManager* cm, int sx, int sy, int sz) {
    WorldResolver resolve(cm);

    struct Node {
        int x, y, z;
        uint8_t oldLight;
    };
    std::vector<Node> removeQueue;
    removeQueue.reserve(4096);

    auto [srcChunk, srcIdx] = resolve(sx, sy, sz);
    if (!srcChunk) return;
    srcChunk->ensureSkyLightFlat();
    uint8_t srcLight = unpackBlock(srcChunk->skyLight.get()[srcIdx]);
    srcChunk->skyLight.get()[srcIdx] = packLight(unpackSky(srcChunk->skyLight.get()[srcIdx]), 0);
    srcChunk->markSectionDirty(sy / 16);
    removeQueue.push_back({sx, sy, sz, srcLight});

    struct LightNode {
        int x, y, z;
    };
    std::vector<LightNode> relightSeeds;

    size_t head = 0;
    while (head < removeQueue.size()) {
        auto [bx, by, bz, oldLight] = removeQueue[head++];
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            nc->ensureSkyLightFlat();
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
    lightQueue.reserve(relightSeeds.size());
    for (auto& s : relightSeeds) lightQueue.push_back(s);
    head = 0;
    while (head < lightQueue.size()) {
        auto [bx, by, bz] = lightQueue[head++];
        auto [chunk, idx] = resolve(bx, by, bz);
        if (!chunk) continue;
        chunk->ensureSkyLightFlat();
        uint8_t light = unpackBlock(chunk->skyLight.get()[idx]);
        if (light <= 1) continue;
        for (auto& d : DIRS_6) {
            int nx = bx + d[0], ny = by + d[1], nz = bz + d[2];
            auto [nc, ni] = resolve(nx, ny, nz);
            if (!nc) continue;
            nc->ensureSkyLightFlat();
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
