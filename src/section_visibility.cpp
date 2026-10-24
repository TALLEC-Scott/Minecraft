#include "section_visibility.h"
#include <cstring>
#include "chunk_section.h"

namespace secvis {

namespace {

// Flood fill over the 16³ passability grid: for each connected
// non-opaque component, collect the set of faces it touches and OR in
// every pair among them. `passable` is indexed x*256 + y*16 + z.
uint16_t connectivityFromGrid(const bool* passable) {
    uint16_t conn = 0;
    static thread_local std::vector<uint16_t> stack;
    uint8_t visited[16 * 16 * 16];
    std::memset(visited, 0, sizeof(visited));

    auto idx = [](int x, int y, int z) { return x * 256 + y * 16 + z; };

    for (int x0 = 0; x0 < 16; x0++) {
        for (int y0 = 0; y0 < 16; y0++) {
            for (int z0 = 0; z0 < 16; z0++) {
                int start = idx(x0, y0, z0);
                if (visited[start] || !passable[start]) continue;

                uint8_t faces = 0; // bit per face touched by this component
                stack.clear();
                stack.push_back(static_cast<uint16_t>(start));
                visited[start] = 1;
                while (!stack.empty()) {
                    int cur = stack.back();
                    stack.pop_back();
                    int x = cur >> 8, y = (cur >> 4) & 15, z = cur & 15;
                    if (x == 15) faces |= 1u << 0; // +X
                    if (x == 0) faces |= 1u << 1;  // -X
                    if (y == 15) faces |= 1u << 2; // +Y
                    if (y == 0) faces |= 1u << 3;  // -Y
                    if (z == 15) faces |= 1u << 4; // +Z
                    if (z == 0) faces |= 1u << 5;  // -Z

                    static constexpr int D[6][3] = {
                        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
                    };
                    for (auto& d : D) {
                        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                        if (nx < 0 || nx > 15 || ny < 0 || ny > 15 || nz < 0 || nz > 15) continue;
                        int ni = idx(nx, ny, nz);
                        if (visited[ni] || !passable[ni]) continue;
                        visited[ni] = 1;
                        stack.push_back(static_cast<uint16_t>(ni));
                    }
                }

                for (int a = 0; a < 6; a++) {
                    if (!(faces & (1u << a))) continue;
                    for (int b = a + 1; b < 6; b++) {
                        if (faces & (1u << b)) conn |= facePairBit(a, b);
                    }
                }
                if (conn == CONN_ALL) return conn; // can't grow further
            }
        }
    }
    return conn;
}

} // namespace

uint16_t computeSectionConnectivity(const Cube* blocks, int sy) {
    bool passable[16 * 16 * 16];
    const int yBase = sy * 16;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            const Cube* col = &blocks[x * CHUNK_HEIGHT * 16 + (yBase + y) * 16];
            for (int z = 0; z < 16; z++) {
                passable[x * 256 + y * 16 + z] = !blocksSight(col[z].getType());
            }
        }
    }
    return connectivityFromGrid(passable);
}

uint16_t computeSectionConnectivity(const ChunkSection* section) {
    if (!section || section->isEmpty()) return CONN_ALL;
    // Palette pre-check: no see-through entry at all → sealed, and we
    // can skip decoding the packed bits entirely.
    bool anyPassable = false;
    for (uint8_t t : section->getPalette()) {
        if (!blocksSight(static_cast<block_type>(t))) {
            anyPassable = true;
            break;
        }
    }
    if (!anyPassable) return CONN_NONE;

    bool passable[16 * 16 * 16];
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++)
                passable[x * 256 + y * 16 + z] = !blocksSight(section->getBlock(x, y, z));
    return connectivityFromGrid(passable);
}

} // namespace secvis
