#include "light_propagation.h"

#include <vector>

#include "ChunkManager.h"
#include "chunk.h"
#include "cube.h"
#include "light_data.h"
#include "world_resolver.h"

using WorldResolver = WorldResolverT<ChunkManager, Chunk>;

void computeBlockLightData(Cube* blocks, uint8_t* skyLight, int maxSolidY) {
    auto idx = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) * CHUNK_HEIGHT * CHUNK_SIZE + static_cast<size_t>(y) * CHUNK_SIZE + z;
    };

    // Clear the block-light nibble (low 4 bits) everywhere; preserve sky.
    const size_t total = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;
    for (size_t i = 0; i < total; i++) skyLight[i] &= 0xF0;

    std::vector<int32_t> queue;
    queue.reserve(64);
    auto packCoord = [](int x, int y, int z) -> int32_t { return (x << 20) | (y << 8) | z; };

    // Seed from emissive blocks.
    int scanH = std::min(maxSolidY + 16, CHUNK_HEIGHT);
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < scanH; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                block_type bt = blocks[idx(x, y, z)].getType();
                uint8_t emit = getBlockLightEmission(bt);
                if (emit == 0) continue;
                skyLight[idx(x, y, z)] = (skyLight[idx(x, y, z)] & 0xF0) | (emit & 0xF);
                queue.push_back(packCoord(x, y, z));
            }

    // BFS. Opaque non-emissive blocks halt the flood; translucent/non-solid
    // cells attenuate by 1 per step.
    size_t head = 0;
    static constexpr int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    while (head < queue.size()) {
        int32_t packed = queue[head++];
        int x = (packed >> 20) & 0xFFF;
        int y = (packed >> 8) & 0xFFF;
        int z = packed & 0xFF;
        uint8_t light = skyLight[idx(x, y, z)] & 0xF;
        if (light <= 1) continue;
        uint8_t propagated = light - 1;
        for (auto& d : DIRS) {
            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_HEIGHT || nz < 0 || nz >= CHUNK_SIZE) continue;
            size_t ni = idx(nx, ny, nz);
            block_type bt = blocks[ni].getType();
            if (hasFlag(bt, BF_OPAQUE) && getBlockLightEmission(bt) == 0) continue;
            if ((skyLight[ni] & 0xF) >= propagated) continue;
            skyLight[ni] = (skyLight[ni] & 0xF0) | (propagated & 0xF);
            queue.push_back(packCoord(nx, ny, nz));
        }
    }
}

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
