#include "terrain_density.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace terraindensity {

static_assert(2 * static_cast<int>(AMPLITUDE) + 3 <= 64, "a column's band must fit its 64-bit mask");

namespace {

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

constexpr uint8_t AIR_CELL = 0, SOLID_CELL = 1, SEEN_CELL = 2;

} // namespace

void shapeColumns(TerrainGenerator& terrain, int wx0, int wz0, int n, const int* height, const float* jagged,
                  ColumnBand* out) {
    const int bandMin = terrain.getSeaLevel() + BAND_FLOOR;
    auto plain = [&](int idx) {
        ColumnBand& b = out[idx];
        b = ColumnBand{};
        b.top = b.floor = height[idx];
    };
    bool any = false;
    for (int idx = 0; idx < n * n; idx++) {
        plain(idx);
        if (jagged[idx] > 0.0f && height[idx] >= bandMin) any = true;
    }
    if (!any) return;

    // --- Window: the square plus a FLOAT_MAX ring ---
    const int W = n + 2 * FLOAT_MAX;
    const int x0 = wx0 - FLOAT_MAX, z0 = wz0 - FLOAT_MAX;
    static thread_local std::vector<int> wh, lo, hi;
    static thread_local std::vector<float> wj;
    wh.assign(static_cast<size_t>(W) * W, 0);
    wj.assign(static_cast<size_t>(W) * W, 0.0f);
    lo.assign(static_cast<size_t>(W) * W, 0);
    hi.assign(static_cast<size_t>(W) * W, -1); // hi < lo: plain column
    for (int i = 0; i < W; i++)
        for (int k = 0; k < W; k++) {
            const int ii = i - FLOAT_MAX, kk = k - FLOAT_MAX;
            const size_t c = static_cast<size_t>(i) * W + k;
            if (ii >= 0 && ii < n && kk >= 0 && kk < n) {
                wh[c] = height[ii * n + kk];
                wj[c] = jagged[ii * n + kk];
            } else {
                TerrainGenerator::ColumnShape s = terrain.getShape(x0 + i, z0 + k);
                wh[c] = s.height;
                wj[c] = s.jagged;
            }
        }

    int gLo = CHUNK_HEIGHT, gHi = -1;
    for (size_t c = 0; c < wh.size(); c++) {
        if (wj[c] <= 0.0f || wh[c] < bandMin) continue;
        const int reach = static_cast<int>(std::ceil(AMPLITUDE * wj[c]));
        lo[c] = std::max(bandMin, wh[c] - reach);
        hi[c] = std::min(CEILING, wh[c] + reach);
        if (hi[c] < lo[c]) continue;
        gLo = std::min(gLo, lo[c]);
        gHi = std::max(gHi, hi[c]);
    }
    if (gHi < gLo) return;
    const int H = gHi - gLo + 1;

    // --- Overhang noise on the world-aligned lattice ---
    const int lx0 = floorDiv(x0, LATTICE), lz0 = floorDiv(z0, LATTICE), ly0 = floorDiv(gLo, LATTICE);
    const int LX = floorDiv(x0 + W - 1, LATTICE) - lx0 + 2;
    const int LZ = floorDiv(z0 + W - 1, LATTICE) - lz0 + 2;
    const int LY = floorDiv(gHi, LATTICE) - ly0 + 2;
    static thread_local std::vector<float> lat;
    lat.resize(static_cast<size_t>(LX) * LY * LZ);
    auto latAt = [&](int a, int b, int c) -> float& {
        return lat[(static_cast<size_t>(a) * LZ + c) * LY + b];
    };
    for (int a = 0; a < LX; a++)
        for (int c = 0; c < LZ; c++)
            for (int b = 0; b < LY; b++)
                latAt(a, b, c) = static_cast<float>(
                    terrain.overhangNoise((lx0 + a) * LATTICE, (ly0 + b) * LATTICE, (lz0 + c) * LATTICE));

    // --- Solid grid over the window's band ---
    static thread_local std::vector<uint8_t> grid;
    grid.assign(static_cast<size_t>(W) * W * H, AIR_CELL);
    auto cell = [&](int i, int k, int y) -> uint8_t& {
        return grid[(static_cast<size_t>(i) * W + k) * H + (y - gLo)];
    };
    constexpr float INV = 1.0f / LATTICE;
    for (int i = 0; i < W; i++)
        for (int k = 0; k < W; k++) {
            const size_t c = static_cast<size_t>(i) * W + k;
            // Plain columns and everything under a band rest on the ground
            // below the window: grounded already.
            if (hi[c] < lo[c]) {
                for (int y = gLo; y <= std::min(wh[c], gHi); y++) cell(i, k, y) = SEEN_CELL;
                continue;
            }
            for (int y = gLo; y < lo[c]; y++) cell(i, k, y) = SEEN_CELL;
            const int wx = x0 + i, wz = z0 + k;
            const int a = floorDiv(wx, LATTICE) - lx0, cc = floorDiv(wz, LATTICE) - lz0;
            const float fx = static_cast<float>(wx - (lx0 + a) * LATTICE) * INV;
            const float fz = static_cast<float>(wz - (lz0 + cc) * LATTICE) * INV;
            const float amp = static_cast<float>(AMPLITUDE) * wj[c];
            const float target = static_cast<float>(wh[c]) + 0.5f;
            for (int y = lo[c]; y <= hi[c]; y++) {
                const int b = floorDiv(y, LATTICE) - ly0;
                const float fy = static_cast<float>(y - (ly0 + b) * LATTICE) * INV;
                auto plane = [&](int bb) {
                    const float n0 = latAt(a, bb, cc) + (latAt(a + 1, bb, cc) - latAt(a, bb, cc)) * fx;
                    const float n1 = latAt(a, bb, cc + 1) + (latAt(a + 1, bb, cc + 1) - latAt(a, bb, cc + 1)) * fx;
                    return n0 + (n1 - n0) * fz;
                };
                const float p0 = plane(b), p1 = plane(b + 1);
                const float noise = p0 + (p1 - p0) * fy;
                if (target - static_cast<float>(y) + amp * noise > 0.0f) cell(i, k, y) = SOLID_CELL;
            }
        }

    // --- Grounded: band blocks connected to the ground (SEEN cells) ---
    static thread_local std::vector<int> stack, piece;
    stack.clear();
    auto unpack = [&](int id, int& i, int& k, int& y) {
        y = id % H + gLo;
        const int col = id / H;
        i = col / W;
        k = col % W;
    };
    auto idOf = [&](int i, int k, int y) { return (i * W + k) * H + (y - gLo); };
    const int NB[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < W; i++)
        for (int k = 0; k < W; k++) {
            const size_t c = static_cast<size_t>(i) * W + k;
            for (int y = lo[c]; y <= hi[c]; y++) {
                if (cell(i, k, y) != SOLID_CELL) continue;
                for (const auto& d : NB) {
                    const int ni = i + d[0], ny = y + d[1], nk = k + d[2];
                    if (ni < 0 || ni >= W || nk < 0 || nk >= W || ny < gLo || ny > gHi) continue;
                    if (cell(ni, nk, ny) == SEEN_CELL) {
                        cell(i, k, y) = SEEN_CELL;
                        stack.push_back(idOf(i, k, y));
                        break;
                    }
                }
            }
        }
    auto flood = [&](std::vector<int>* collect) {
        while (!stack.empty()) {
            const int id = stack.back();
            stack.pop_back();
            if (collect) collect->push_back(id);
            int i = 0, k = 0, y = 0;
            unpack(id, i, k, y);
            for (const auto& d : NB) {
                const int ni = i + d[0], ny = y + d[1], nk = k + d[2];
                if (ni < 0 || ni >= W || nk < 0 || nk >= W || ny < gLo || ny > gHi) continue;
                uint8_t& v = cell(ni, nk, ny);
                if (v != SOLID_CELL) continue;
                v = SEEN_CELL;
                stack.push_back(idOf(ni, nk, ny));
            }
        }
    };
    flood(nullptr);

    // --- Floating pieces: drop the small ones seen whole ---
    for (int i = 0; i < W; i++)
        for (int k = 0; k < W; k++)
            for (int y = lo[static_cast<size_t>(i) * W + k]; y <= hi[static_cast<size_t>(i) * W + k]; y++) {
                if (cell(i, k, y) != SOLID_CELL) continue;
                cell(i, k, y) = SEEN_CELL;
                piece.clear();
                stack.push_back(idOf(i, k, y));
                flood(&piece);
                int minI = W, maxI = -1, minK = W, maxK = -1;
                for (int id : piece) {
                    int pi = 0, pk = 0, py = 0;
                    unpack(id, pi, pk, py);
                    minI = std::min(minI, pi);
                    maxI = std::max(maxI, pi);
                    minK = std::min(minK, pk);
                    maxK = std::max(maxK, pk);
                }
                const bool edge = minI == 0 || minK == 0 || maxI == W - 1 || maxK == W - 1;
                if (edge || maxI - minI >= FLOAT_MAX || maxK - minK >= FLOAT_MAX) continue;
                for (int id : piece) grid[static_cast<size_t>(id)] = AIR_CELL;
            }

    // --- Bands of the requested square ---
    for (int ii = 0; ii < n; ii++)
        for (int kk = 0; kk < n; kk++) {
            const int i = ii + FLOAT_MAX, k = kk + FLOAT_MAX;
            const size_t c = static_cast<size_t>(i) * W + k;
            if (hi[c] < lo[c]) continue; // plain: already set
            ColumnBand& b = out[ii * n + kk];
            b.lo = lo[c];
            b.count = hi[c] - lo[c] + 1;
            b.solid = 0;
            b.top = lo[c] - 1;
            for (int y = lo[c]; y <= hi[c]; y++)
                if (cell(i, k, y) != AIR_CELL) {
                    b.solid |= uint64_t{1} << (y - lo[c]);
                    b.top = y;
                }
            b.floor = lo[c] - 1;
            while (b.floor < hi[c] && cell(i, k, b.floor + 1) != AIR_CELL) b.floor++;
        }
}

void surfaceHeights(TerrainGenerator& terrain, int chunkX, int chunkZ, int (&heights)[CHUNK_SIZE][CHUNK_SIZE]) {
    constexpr int N = CHUNK_SIZE;
    int h[N * N];
    float j[N * N];
    ColumnBand bands[N * N];
    const int ox = chunkX * N, oz = chunkZ * N;
    for (int i = 0; i < N; i++)
        for (int k = 0; k < N; k++) {
            TerrainGenerator::ColumnShape s = terrain.getShape(ox + i, oz + k);
            h[i * N + k] = s.height;
            j[i * N + k] = s.jagged;
        }
    shapeColumns(terrain, ox, oz, N, h, j, bands);
    for (int i = 0; i < N; i++)
        for (int k = 0; k < N; k++) heights[i][k] = bands[i * N + k].top;
}

} // namespace terraindensity
