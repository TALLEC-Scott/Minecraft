// Tests for the SparseSkyLight flat/sparse dual-storage invariants.
//
// The sparse storage is the authoritative form. The flat array is a
// lazily-populated cache used by hot paths (mesh builds, BFS). Writes
// can go to either form, but they must be synced explicitly (via
// loadFromFlat / exportToFlat) — the storage doesn't auto-reconcile.
//
// These tests cover the sync contract AND simulate the world-level
// BFS pattern that the real destroyBlock/removeBlockLightWorld paths
// follow, to lock in the fix that readers must consult the flat array
// first when it's allocated.

#include <gtest/gtest.h>
#include "sparse_skylight.h"
#include "light_data.h"
#include <cstring>
#include <vector>

// Total bytes in a flat chunk light buffer.
static constexpr size_t FLAT_BYTES = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE;

// Build a flat buffer with a uniform value — matches the post-generation
// state (open sky = 0xF0 packed, deep underground = 0x00 packed).
static std::vector<uint8_t> makeUniformFlat(uint8_t packed) {
    std::vector<uint8_t> buf(FLAT_BYTES, packed);
    return buf;
}

TEST(SparseSkyLight, RoundTripUniform) {
    // Uniform sections should compress to zero heap allocation.
    SparseSkyLight sl;
    auto flat = makeUniformFlat(packLight(15, 0)); // full sky, no block light
    sl.loadFromFlat(flat.data());

    // All reads return the uniform value.
    for (int y = 0; y < CHUNK_HEIGHT; y += 16)
        EXPECT_EQ(sl.get(0, y, 0), packLight(15, 0));

    // Round-trip to another flat — values preserved.
    std::vector<uint8_t> out(FLAT_BYTES, 0xAA);
    sl.exportToFlat(out.data());
    EXPECT_EQ(std::memcmp(flat.data(), out.data(), FLAT_BYTES), 0);
}

TEST(SparseSkyLight, PerSectionPromotion) {
    // Mixed sections allocate a 4 KB array; uniform sections stay null.
    SparseSkyLight sl;
    auto flat = makeUniformFlat(packLight(15, 0));
    // Insert one differing cell in section 4 (y=64..79).
    flat[lightIdx(5, 70, 7)] = packLight(10, 3);
    sl.loadFromFlat(flat.data());

    EXPECT_EQ(sl.get(5, 70, 7), packLight(10, 3));
    EXPECT_EQ(sl.get(0, 70, 0), packLight(15, 0));  // same section, still uniform bg
    EXPECT_EQ(sl.get(0, 32, 0), packLight(15, 0));  // different section, still uniform

    // Only the mixed section consumed memory.
    // sizeof(self) + 1 × 4096 for the one promoted section.
    size_t expected = sizeof(SparseSkyLight) + SparseSkyLight::SECTION_VOL;
    EXPECT_EQ(sl.memoryUsage(), expected);
}

// This test reproduces the destroy-glowstone bug:
//  1. Place glowstone: flat is populated with emission, sparse synced.
//  2. Chunk renders, uploadMesh compresses flat → sparse, flat dropped.
//  3. Destroy glowstone: ensureSkyLightFlat reallocates flat FROM sparse,
//     BFS zeros cells in flat, but DOES NOT sync back to sparse.
//  4. Mesh rebuilds while flat is still allocated → must read flat.
//
// If a reader uses sparse at step 4, it sees the old (pre-destroy)
// values — which is what snapshotBorders used to do.
TEST(SparseSkyLight, ReadersMustPreferFlatWhenAllocated) {
    SparseSkyLight sl;

    // Step 1-2: Glowstone placed at (5, 70, 7) with block light 15.
    // After uploadMesh, sparse holds that state; flat is dropped.
    auto flat1 = makeUniformFlat(packLight(15, 0));
    flat1[lightIdx(5, 70, 7)] = packLight(15, 15); // glowstone
    flat1[lightIdx(5, 70, 8)] = packLight(15, 14); // propagated
    flat1[lightIdx(5, 70, 6)] = packLight(15, 14); // propagated
    sl.loadFromFlat(flat1.data());

    // Sanity: sparse reflects the lit state.
    EXPECT_EQ(unpackBlock(sl.get(5, 70, 7)), 15);
    EXPECT_EQ(unpackBlock(sl.get(5, 70, 8)), 14);

    // Step 3: Destroy. Rehydrate flat from sparse (ensureSkyLightFlat).
    std::vector<uint8_t> flat2(FLAT_BYTES);
    sl.exportToFlat(flat2.data());

    // BFS zeros the source and propagated cells — writes go to FLAT only.
    flat2[lightIdx(5, 70, 7)] = packLight(unpackSky(flat2[lightIdx(5, 70, 7)]), 0);
    flat2[lightIdx(5, 70, 8)] = packLight(unpackSky(flat2[lightIdx(5, 70, 8)]), 0);
    flat2[lightIdx(5, 70, 6)] = packLight(unpackSky(flat2[lightIdx(5, 70, 6)]), 0);

    // Step 4 — the bug: sparse is still stale, flat has the truth.
    // Any reader that consults sparse directly (as snapshotBorders did)
    // will see the old glowstone light. This test documents the invariant
    // that callers must prefer flat when it is live.
    EXPECT_EQ(unpackBlock(sl.get(5, 70, 7)), 15) << "sparse still holds pre-destroy state";
    EXPECT_EQ(unpackBlock(flat2[lightIdx(5, 70, 7)]), 0) << "flat has the correct post-destroy state";

    // Simulates the fixed reader: prefer flat when available.
    auto readCorrectly = [&](const uint8_t* flat, int x, int y, int z) -> uint8_t {
        if (flat) return flat[lightIdx(x, y, z)];
        return sl.get(x, y, z);
    };
    EXPECT_EQ(unpackBlock(readCorrectly(flat2.data(), 5, 70, 7)), 0);
    EXPECT_EQ(unpackBlock(readCorrectly(nullptr, 5, 70, 7)), 15); // sparse fallback

    // After a subsequent compress (uploadMesh), sparse catches up.
    sl.loadFromFlat(flat2.data());
    EXPECT_EQ(unpackBlock(sl.get(5, 70, 7)), 0);
    EXPECT_EQ(unpackBlock(sl.get(5, 70, 8)), 0);
}

// Cross-chunk BFS simulation: two adjacent chunks, glowstone near the
// shared edge. Removing it should zero block light on BOTH sides if the
// BFS correctly writes to each chunk's flat array.
TEST(SparseSkyLight, CrossChunkRemovalViaFlat) {
    SparseSkyLight slA, slB;

    // Initial: chunk A has glowstone at x=15 (east edge), block light
    // propagated into chunk B's x=0 (west edge).
    auto flatA = makeUniformFlat(packLight(15, 0));
    auto flatB = makeUniformFlat(packLight(15, 0));
    flatA[lightIdx(15, 70, 8)] = packLight(15, 15); // glowstone on A's edge
    flatA[lightIdx(14, 70, 8)] = packLight(15, 14); // propagated inside A
    flatB[lightIdx(0, 70, 8)]  = packLight(15, 14); // propagated across edge
    flatB[lightIdx(1, 70, 8)]  = packLight(15, 13);
    slA.loadFromFlat(flatA.data());
    slB.loadFromFlat(flatB.data());

    // Drop flat (simulating post-uploadMesh compressSkyLight).
    // Re-hydrate for the destroy BFS.
    std::vector<uint8_t> ha(FLAT_BYTES), hb(FLAT_BYTES);
    slA.exportToFlat(ha.data());
    slB.exportToFlat(hb.data());

    // BFS removal writes to flats only (no sparse sync).
    ha[lightIdx(15, 70, 8)] = packLight(unpackSky(ha[lightIdx(15, 70, 8)]), 0);
    ha[lightIdx(14, 70, 8)] = packLight(unpackSky(ha[lightIdx(14, 70, 8)]), 0);
    hb[lightIdx(0, 70, 8)]  = packLight(unpackSky(hb[lightIdx(0, 70, 8)]), 0);
    hb[lightIdx(1, 70, 8)]  = packLight(unpackSky(hb[lightIdx(1, 70, 8)]), 0);

    // Neighbor B's border reads: if snapshotBorders reads sparse, it sees
    // the stale glow (bug). If it reads flat, it sees zero (fixed).
    auto readBorder = [](const uint8_t* flat, const SparseSkyLight& sparse,
                         int x, int y, int z) -> uint8_t {
        if (flat) return flat[lightIdx(x, y, z)];
        return sparse.get(x, y, z);
    };

    EXPECT_EQ(unpackBlock(readBorder(hb.data(), slB, 0, 70, 8)), 0)
        << "fixed reader sees the BFS write on B's edge";
    EXPECT_EQ(unpackBlock(readBorder(nullptr, slB, 0, 70, 8)), 14)
        << "pre-fix sparse-only reader would still see the propagated glow";
}
