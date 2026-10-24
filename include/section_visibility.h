#pragma once
// Cave culling (Minecraft 1.8 "Checchi" section visibility), GL-free.
//
// Each 16³ chunk section gets a 15-bit connectivity mask: bit (a,b) set
// means a sight line can pass through the section's air between face a
// and face b (they touch the same flood-filled non-opaque component).
// Per frame, a BFS from the camera's section walks the section graph,
// only stepping from section to section where the masks allow, and only
// sections the BFS reaches are rendered — so caves sealed behind rock
// emit no draw calls at all.
//
// Faces are indexed so that opposite(f) == (f ^ 1):
//   0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z
#include <cstdint>
#include <vector>
#include "cube.h"

class ChunkSection;

namespace secvis {

constexpr int NUM_SECTIONS = CHUNK_HEIGHT / 16; // 8

constexpr uint16_t CONN_ALL = 0x7FFF;  // all 15 face pairs connected
constexpr uint16_t CONN_NONE = 0x0000; // fully sealed

inline int oppositeFace(int f) { return f ^ 1; }

// Mask bit for the unordered face pair {a, b}, a != b.
inline int facePairBit(int a, int b) {
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    // Pairs with first face a start after the sum of (5-i) for i < a.
    return (1 << (a * 5 - a * (a - 1) / 2 + (b - a - 1)));
}

inline bool connConnected(uint16_t mask, int a, int b) { return (mask & facePairBit(a, b)) != 0; }

// A block propagates visibility iff it is not fully opaque — the same
// rule the mesher uses to decide a face is hidden.
inline bool blocksSight(block_type t) { return hasFlag(t, BF_OPAQUE); }

// Connectivity from a whole-chunk flat block snapshot (the worker-thread
// mesh path). Layout: blocks[x * CHUNK_HEIGHT * 16 + y * 16 + z],
// section sy covers y in [sy*16, sy*16 + 16).
uint16_t computeSectionConnectivity(const Cube* blocks, int sy);

// Connectivity straight from a palette-encoded section (the online mesh
// path). nullptr or all-air → CONN_ALL; a palette with no see-through
// entry → CONN_NONE without touching the packed bits.
uint16_t computeSectionConnectivity(const ChunkSection* section);

// Checchi BFS over the section graph. Calls mark(cx, cz, sy) for every
// section the camera can potentially see; the caller ORs these into
// per-chunk visibleSections masks. getMask(cx, cz, sy, loaded) returns
// the section's connectivity; setting loaded=false stops traversal into
// that chunk. Sections are visited at most once (first arrival wins —
// conservative and cheap, same as Minecraft).
//
// The start section expands through all 6 faces unconditionally so a
// camera inside solid rock or an unmeshed section never blacks out.
template <typename GetMask, typename Mark>
void caveCullBFS(int startCx, int startCz, int startSy, int maxChunkRadius, GetMask&& getMask, Mark&& mark) {
    struct Node {
        int16_t dx, dz;      // chunk offset from start
        int8_t sy;           // section index
        uint8_t enteredFace; // 6 = start node
        uint8_t dirSet;      // directions already travelled (bit per face)
    };
    const int side = 2 * maxChunkRadius + 1;
    // Scratch reused across frames — sized for the largest radius seen.
    static thread_local std::vector<uint8_t> visited;
    static thread_local std::vector<Node> queue;
    visited.assign(static_cast<size_t>(side) * side * NUM_SECTIONS, 0);
    queue.clear();

    auto visitedIdx = [&](int dx, int dz, int sy) {
        return (static_cast<size_t>(dx + maxChunkRadius) * side + (dz + maxChunkRadius)) * NUM_SECTIONS + sy;
    };

    if (startSy < 0) startSy = 0;
    if (startSy >= NUM_SECTIONS) startSy = NUM_SECTIONS - 1;
    visited[visitedIdx(0, 0, startSy)] = 1;
    queue.push_back({0, 0, static_cast<int8_t>(startSy), 6, 0});
    mark(startCx, startCz, startSy);

    // Face -> neighbor step. Y faces move sy; X/Z faces move chunks.
    static constexpr int STEP[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };

    for (size_t head = 0; head < queue.size(); head++) {
        Node n = queue[head]; // by value: queue may reallocate
        bool loaded = true;
        uint16_t mask = getMask(startCx + n.dx, startCz + n.dz, n.sy, loaded);
        if (!loaded) continue;

        for (int d = 0; d < 6; d++) {
            // Never step back against a direction already travelled.
            if (n.dirSet & (1u << oppositeFace(d))) continue;
            // Interior sections must have a sight line entry→exit.
            if (n.enteredFace != 6 && !connConnected(mask, n.enteredFace, d)) continue;

            int ndx = n.dx + STEP[d][0];
            int ndz = n.dz + STEP[d][2];
            int nsy = n.sy + STEP[d][1];
            if (nsy < 0 || nsy >= NUM_SECTIONS) continue;
            if (ndx < -maxChunkRadius || ndx > maxChunkRadius || ndz < -maxChunkRadius || ndz > maxChunkRadius)
                continue;
            uint8_t& seen = visited[visitedIdx(ndx, ndz, nsy)];
            if (seen) continue;
            seen = 1;
            mark(startCx + ndx, startCz + ndz, nsy);
            queue.push_back({static_cast<int16_t>(ndx), static_cast<int16_t>(ndz), static_cast<int8_t>(nsy),
                             static_cast<uint8_t>(oppositeFace(d)), static_cast<uint8_t>(n.dirSet | (1u << d))});
        }
    }
}

} // namespace secvis
