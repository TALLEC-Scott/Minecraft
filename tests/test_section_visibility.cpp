// Cave-culling building blocks: per-section air connectivity masks and
// the camera BFS over the section graph (section_visibility.h).
#include <gtest/gtest.h>
#include <cstring>
#include <map>
#include <set>
#include "chunk_section.h"
#include "section_visibility.h"

using namespace secvis;

namespace {

// Whole-chunk flat buffer helper (layout x*CHUNK_HEIGHT*16 + y*16 + z).
struct FlatChunk {
    std::vector<Cube> blocks;
    FlatChunk() : blocks(static_cast<size_t>(16) * CHUNK_HEIGHT * 16) {}
    void fillSection(int sy, block_type t) {
        for (int x = 0; x < 16; x++)
            for (int y = sy * 16; y < sy * 16 + 16; y++)
                for (int z = 0; z < 16; z++) set(x, y, z, t);
    }
    void set(int x, int y, int z, block_type t) { blocks[x * CHUNK_HEIGHT * 16 + y * 16 + z].setType(t); }
    const Cube* data() const { return blocks.data(); }
};

} // namespace

TEST(SectionVisibility, FacePairBitsAreDistinct) {
    std::set<int> seen;
    for (int a = 0; a < 6; a++)
        for (int b = 0; b < 6; b++) {
            if (a == b) continue;
            int bit = facePairBit(a, b);
            EXPECT_EQ(bit, facePairBit(b, a)); // unordered
            seen.insert(bit);
        }
    EXPECT_EQ(seen.size(), 15u);
    int all = 0;
    for (int b : seen) all |= b;
    EXPECT_EQ(all, CONN_ALL);
}

TEST(SectionVisibility, SolidSectionSealed) {
    FlatChunk c;
    c.fillSection(2, STONE);
    EXPECT_EQ(computeSectionConnectivity(c.data(), 2), CONN_NONE);
}

TEST(SectionVisibility, EmptySectionFullyConnected) {
    FlatChunk c; // all AIR
    EXPECT_EQ(computeSectionConnectivity(c.data(), 3), CONN_ALL);
}

TEST(SectionVisibility, NullOrEmptyChunkSectionFullyConnected) {
    EXPECT_EQ(computeSectionConnectivity(nullptr), CONN_ALL);
    ChunkSection s;
    EXPECT_EQ(computeSectionConnectivity(&s), CONN_ALL);
}

TEST(SectionVisibility, AllOpaquePaletteSealedWithoutDecoding) {
    ChunkSection s;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++) s.setBlock(x, y, z, STONE);
    // Palette still contains AIR from construction — overwrite check is
    // on actual cells, so this exercises the full flood-fill path too.
    EXPECT_EQ(computeSectionConnectivity(&s), CONN_NONE);
}

TEST(SectionVisibility, SeeThroughBlocksPropagate) {
    // Water, leaves, and plants must connect faces like air does.
    for (block_type t : {WATER, LEAVES, TALL_GRASS}) {
        FlatChunk c;
        c.fillSection(1, t);
        EXPECT_EQ(computeSectionConnectivity(c.data(), 1), CONN_ALL) << "block " << t;
    }
}

TEST(SectionVisibility, LTunnelConnectsExactlyItsFaces) {
    // Solid section with an L-shaped tunnel: enters at -X (y=8,z=8),
    // runs to the center, turns up and exits at +Y (x=8,z=8).
    FlatChunk c;
    c.fillSection(0, STONE);
    const int yBase = 0;
    for (int x = 0; x <= 8; x++) c.set(x, yBase + 8, 8, AIR);
    for (int y = 8; y < 16; y++) c.set(8, yBase + y, 8, AIR);
    uint16_t conn = computeSectionConnectivity(c.data(), 0);
    EXPECT_TRUE(connConnected(conn, 1, 2));  // -X <-> +Y
    EXPECT_FALSE(connConnected(conn, 0, 1)); // +X unreachable
    EXPECT_FALSE(connConnected(conn, 3, 2)); // -Y unreachable
    EXPECT_FALSE(connConnected(conn, 4, 5));
}

TEST(SectionVisibility, SealedPocketContributesNothing) {
    // A solid section with an interior air room that touches no face.
    FlatChunk c;
    c.fillSection(4, STONE);
    for (int x = 4; x <= 11; x++)
        for (int y = 4; y <= 11; y++)
            for (int z = 4; z <= 11; z++) c.set(x, 64 + y, z, AIR);
    EXPECT_EQ(computeSectionConnectivity(c.data(), 4), CONN_NONE);
}

TEST(SectionVisibility, TwoComponentsEachConnectTheirOwnFaces) {
    // Slab wall at y=8 splits the section: lower half touches -Y, upper
    // half touches +Y; neither connects across.
    FlatChunk c;
    const int sy = 5, yBase = sy * 16;
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) c.set(x, yBase + 8, z, STONE);
    uint16_t conn = computeSectionConnectivity(c.data(), sy);
    EXPECT_FALSE(connConnected(conn, 2, 3)); // +Y <-> -Y blocked
    EXPECT_TRUE(connConnected(conn, 0, 1));  // sides connect (both halves)
    EXPECT_TRUE(connConnected(conn, 0, 2));  // upper half: +X <-> +Y
    EXPECT_TRUE(connConnected(conn, 0, 3));  // lower half: +X <-> -Y
}

// --- BFS ---

namespace {

// Tiny synthetic world: map from (cx,cz,sy) to connectivity mask.
struct MaskWorld {
    std::map<std::tuple<int, int, int>, uint16_t> masks;
    uint16_t fallback = CONN_NONE;
    bool loaded = true;

    auto getMask() {
        return [this](int cx, int cz, int sy, bool& okLoaded) -> uint16_t {
            okLoaded = loaded;
            auto it = masks.find({cx, cz, sy});
            return it != masks.end() ? it->second : fallback;
        };
    }
};

using Visited = std::set<std::tuple<int, int, int>>;

Visited runBFS(MaskWorld& w, int cx, int cz, int sy, int radius) {
    Visited v;
    caveCullBFS(cx, cz, sy, radius, w.getMask(), [&](int x, int z, int s) { v.insert({x, z, s}); });
    return v;
}

} // namespace

TEST(SectionVisibility, BfsOpenWorldReachesEverything) {
    MaskWorld w;
    w.fallback = CONN_ALL;
    Visited v = runBFS(w, 0, 0, 4, 2);
    // 5x5 chunks x 8 sections all reachable in an open world.
    EXPECT_EQ(v.size(), 5u * 5u * NUM_SECTIONS);
}

TEST(SectionVisibility, BfsSealedNeighborsStopPropagation) {
    MaskWorld w;
    w.fallback = CONN_NONE; // every section sealed
    Visited v = runBFS(w, 0, 0, 4, 2);
    // Start + its 6 face neighbors get marked (their MESH is visible as
    // walls), but nothing propagates through sealed sections.
    EXPECT_TRUE(v.count({0, 0, 4}));
    EXPECT_TRUE(v.count({1, 0, 4}));
    EXPECT_TRUE(v.count({0, 0, 5}));
    EXPECT_EQ(v.size(), 7u);
}

TEST(SectionVisibility, BfsCorridorFollowsConnectivity) {
    // Chain of sections along +X connected only X<->X; a side branch is
    // sealed. BFS must reach the chain but not hop the seal.
    MaskWorld w;
    w.fallback = CONN_NONE;
    uint16_t xTube = static_cast<uint16_t>(facePairBit(0, 1));
    for (int cx = 0; cx <= 3; cx++) w.masks[{cx, 0, 2}] = xTube;
    Visited v = runBFS(w, 0, 0, 2, 4);
    EXPECT_TRUE(v.count({3, 0, 2}));  // end of corridor
    EXPECT_FALSE(v.count({2, 1, 2})); // corridor wall's far side...
    EXPECT_FALSE(v.count({1, 0, 3})); // no vertical leak past neighbors of start
}

TEST(SectionVisibility, BfsDirectionConstraintNoBacktrack) {
    // Everything fully connected: BFS may wander, but the no-backtrack
    // rule means it still terminates and marks each section once.
    MaskWorld w;
    w.fallback = CONN_ALL;
    int marks = 0;
    caveCullBFS(0, 0, 0, 3, w.getMask(), [&](int, int, int) { marks++; });
    EXPECT_EQ(marks, 7 * 7 * NUM_SECTIONS); // each marked exactly once
}

TEST(SectionVisibility, BfsStartExpandsAllFacesEvenIfSealed) {
    // Camera inside solid rock: start section mask CONN_NONE, but its
    // neighbors must still be marked (no black screen), and if a
    // neighbor is open the flood continues from there.
    MaskWorld w;
    w.fallback = CONN_NONE;
    w.masks[{1, 0, 4}] = CONN_ALL;
    Visited v = runBFS(w, 0, 0, 4, 2);
    EXPECT_TRUE(v.count({2, 0, 4})); // through the open neighbor
    EXPECT_TRUE(v.count({0, 0, 3})); // direct neighbor of start
}

TEST(SectionVisibility, BfsUnloadedChunkStops) {
    MaskWorld w;
    w.fallback = CONN_ALL;
    w.loaded = false; // nothing is loaded (start included)
    Visited v = runBFS(w, 0, 0, 4, 2);
    // Start is marked before its mask is queried, but its expansion is
    // skipped once the chunk reports unloaded — nothing else is marked.
    EXPECT_EQ(v.size(), 1u);
}

TEST(SectionVisibility, BfsClampsStartSection) {
    MaskWorld w;
    w.fallback = CONN_ALL;
    Visited v = runBFS(w, 0, 0, 99, 1); // camera above the world
    EXPECT_TRUE(v.count({0, 0, NUM_SECTIONS - 1}));
}
