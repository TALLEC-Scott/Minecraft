// Regression tests for WorldResolverT.
//
// The resolver was the root cause of the "glowstone destroy leaves light
// behind" bug: before the fix, its operator() checked `!chunk->skyLight`
// and returned nullptr for chunks whose light was held in sparse form
// (every chunk after its first mesh upload). All world-level light BFS
// functions bailed silently on their source cell. The fix drops that
// guard — the resolver only reports whether the chunk *exists*; the
// caller is responsible for ensureSkyLightFlat() before reading.

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "cube.h"
#include "world_resolver.h"

namespace {

// Minimal fakes — just enough to instantiate WorldResolverT. We model the
// sparse-storage "skyLight may be null" shape explicitly so the test
// locks in the invariant the bug violated.
struct FakeChunk {
    int chunkX, chunkZ;
    // Mirrors the real Chunk::skyLight (shared_ptr<uint8_t[]>). When
    // compressed into sparse form, this is null — exactly the state the
    // old resolver mistook for "chunk doesn't exist."
    std::shared_ptr<uint8_t[]> skyLight;
};

struct FakeChunkManager {
    std::unordered_map<long long, std::unique_ptr<FakeChunk>> chunks;

    static long long key(int cx, int cz) {
        return (static_cast<long long>(cx) << 32) ^ (cz & 0xffffffffLL);
    }

    FakeChunk* getChunk(int cx, int cz) {
        auto it = chunks.find(key(cx, cz));
        return it == chunks.end() ? nullptr : it->second.get();
    }

    FakeChunk* addChunk(int cx, int cz, bool allocFlat) {
        auto c = std::make_unique<FakeChunk>();
        c->chunkX = cx;
        c->chunkZ = cz;
        if (allocFlat) {
            c->skyLight = std::shared_ptr<uint8_t[]>(
                new uint8_t[static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE]());
        }
        FakeChunk* raw = c.get();
        chunks[key(cx, cz)] = std::move(c);
        return raw;
    }
};

using Resolver = WorldResolverT<FakeChunkManager, FakeChunk>;

} // namespace

TEST(WorldResolver, ResolvesChunkWithAllocatedFlatLight) {
    FakeChunkManager cm;
    FakeChunk* c = cm.addChunk(0, 0, /*allocFlat=*/true);
    Resolver r(&cm);

    auto [chunk, idx] = r(5, 70, 7);
    EXPECT_EQ(chunk, c);
    EXPECT_EQ(idx, static_cast<size_t>(5) * CHUNK_HEIGHT * CHUNK_SIZE + 70 * CHUNK_SIZE + 7);
}

// The regression test. Before the fix, this returned nullptr and every
// destroyBlock / placeBlock light update silently did nothing.
TEST(WorldResolver, ResolvesChunkEvenWhenSkyLightIsNull) {
    FakeChunkManager cm;
    FakeChunk* c = cm.addChunk(0, 0, /*allocFlat=*/false);
    ASSERT_EQ(c->skyLight.get(), nullptr);
    Resolver r(&cm);

    auto [chunk, idx] = r(5, 70, 7);
    EXPECT_EQ(chunk, c) << "resolver must return sparse-only chunks; caller is "
                          "responsible for ensureSkyLightFlat() before reading";
    EXPECT_EQ(idx, static_cast<size_t>(5) * CHUNK_HEIGHT * CHUNK_SIZE + 70 * CHUNK_SIZE + 7);
}

TEST(WorldResolver, ReturnsNullForMissingChunk) {
    FakeChunkManager cm; // empty
    Resolver r(&cm);

    auto [chunk, idx] = r(5, 70, 7);
    EXPECT_EQ(chunk, nullptr);
    EXPECT_EQ(idx, 0u);
}

TEST(WorldResolver, ReturnsNullOutOfYBounds) {
    FakeChunkManager cm;
    cm.addChunk(0, 0, true);
    Resolver r(&cm);

    EXPECT_EQ(r(5, -1, 7).first, nullptr);
    EXPECT_EQ(r(5, CHUNK_HEIGHT, 7).first, nullptr);
    EXPECT_EQ(r(5, CHUNK_HEIGHT + 100, 7).first, nullptr);
}

TEST(WorldResolver, CachesLastChunkLookup) {
    FakeChunkManager cm;
    FakeChunk* c = cm.addChunk(2, -3, true);
    Resolver r(&cm);

    // First call populates the cache.
    auto first = r(2 * CHUNK_SIZE + 4, 70, -3 * CHUNK_SIZE + 1);
    EXPECT_EQ(first.first, c);
    EXPECT_EQ(r.cachedChunk, c);
    EXPECT_EQ(r.cachedCX, 2);
    EXPECT_EQ(r.cachedCZ, -3);

    // Second call in the same chunk — cache still matches.
    auto second = r(2 * CHUNK_SIZE + 9, 40, -3 * CHUNK_SIZE + 10);
    EXPECT_EQ(second.first, c);
}

TEST(WorldResolver, CrossChunkResolves) {
    FakeChunkManager cm;
    FakeChunk* a = cm.addChunk(0, 0, /*allocFlat=*/true);
    FakeChunk* b = cm.addChunk(1, 0, /*allocFlat=*/false); // sparse-only
    Resolver r(&cm);

    auto left = r(CHUNK_SIZE - 1, 70, 8); // last column of chunk 0
    auto right = r(CHUNK_SIZE, 70, 8);    // first column of chunk 1 (sparse)

    EXPECT_EQ(left.first, a);
    EXPECT_EQ(right.first, b)
        << "cross-chunk BFS must reach the neighbor even if it's sparse-only";
}
