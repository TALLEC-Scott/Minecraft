#pragma once

// Cached world-coordinate resolver: given (wx, wy, wz), look up the
// owning Chunk and local coordinates. Holds the last chunk pointer so
// consecutive queries within the same chunk skip the hash-map lookup.
// Used by light BFS, water simulation, and anything else that walks
// neighbors across chunk boundaries.
//
// Templated so tests can instantiate it with fake Chunk / ChunkManager
// types and exercise the resolver without pulling in GL-dependent chunk.h.
// Production code uses the `WorldResolver` alias in world.h.

#include <climits>
#include <cstddef>
#include <utility>
#include "cube.h"  // CHUNK_HEIGHT, CHUNK_SIZE, worldToChunk, worldToLocal

template <typename ChunkManagerT, typename ChunkT>
struct WorldResolverT {
    ChunkManagerT* cm;
    int cachedCX = INT_MIN, cachedCZ = INT_MIN;
    ChunkT* cachedChunk = nullptr;

    explicit WorldResolverT(ChunkManagerT* cm) : cm(cm) {}

    struct Local {
        ChunkT* chunk;
        int lx;
        int lz;
    };

    // Local-coordinate resolution; caller handles y-bounds.
    Local local(int wx, int wz) {
        int cx = worldToChunk(wx), cz = worldToChunk(wz);
        if (cx != cachedCX || cz != cachedCZ) {
            cachedCX = cx;
            cachedCZ = cz;
            cachedChunk = cm->getChunk(cx, cz);
        }
        return {cachedChunk, worldToLocal(wx, cx), worldToLocal(wz, cz)};
    }

    // Flat index into the chunk's packed skyLight / cube arrays. Returns
    // {nullptr, 0} if out of bounds or the chunk is missing. The caller
    // is responsible for ensureSkyLightFlat() before dereferencing
    // skyLight.get() — the chunk may hold its light in sparse form only.
    // (Before the sparse refactor this check also gated on !skyLight;
    // that guard confused sparse chunks with dead ones and silently
    // suppressed every post-upload light update.)
    std::pair<ChunkT*, size_t> operator()(int wx, int wy, int wz) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return {nullptr, 0};
        auto loc = local(wx, wz);
        if (!loc.chunk) return {nullptr, 0};
        return {loc.chunk,
                static_cast<size_t>(loc.lx) * CHUNK_HEIGHT * CHUNK_SIZE +
                    static_cast<size_t>(wy) * CHUNK_SIZE + loc.lz};
    }
};
