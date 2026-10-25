// Offline (GL-free) mesh builder. Mirrors the online path in chunk.cpp
// but reads neighbor data from a snapshotted NeighborBorders so the
// function is safe to call from a worker thread. Split out so tests can
// exercise it without needing an OpenGL context.

#include "chunk_mesh.h"

#include <cmath>

#include "block_layers.h"
#include "cube.h"
#include "light_data.h"
#include "light_sampler.h"
#include "mesh_types.h"
#include "tracy_shim.h"

#include <algorithm>
#include <cstdint>
#include <vector>

static constexpr int BYTES_PER_VERT = sizeof(PackedVertex);
static constexpr int WATER_BYTES_PER_VERT = sizeof(WaterVertex);

// Reads in-chunk from blocks[]/waterLevels[], cross-chunk from the
// snapshotted NeighborBorders (cardinal edges + diagonal corners). Only
// cells of `fluid` (the liquid being meshed) count as fluid, so water and
// lava each slope against themselves.
static WaterCellSample sampleBorder(const Cube* blocks, const uint8_t* waterLevels, const NeighborBorders& nb, int bx,
                                    int by, int bz, block_type fluid) {
    auto classify = [fluid](block_type t, uint8_t raw) -> WaterCellSample {
        if (t == fluid) return {CellKind::Water, raw};
        if (t == AIR) return {CellKind::Air, 0};
        return {CellKind::Solid, 0};
    };
    if (by < 0 || by >= CHUNK_HEIGHT) return {CellKind::Solid, 0};
    bool xNeg = bx < 0, xPos = bx >= CHUNK_SIZE;
    bool zNeg = bz < 0, zPos = bz >= CHUNK_SIZE;
    if ((xNeg || xPos) && (zNeg || zPos)) {
        const DiagonalCorner* d = nullptr;
        if (xNeg && zNeg)
            d = &nb.dNN;
        else if (xNeg && zPos)
            d = &nb.dNP;
        else if (xPos && zNeg)
            d = &nb.dPN;
        else
            d = &nb.dPP;
        if (!d->valid) return {CellKind::Solid, 0};
        return classify(d->types[by], d->waterBorder[by]);
    }
    if (xNeg || xPos || zNeg || zPos) {
        const NeighborBorder* b = nullptr;
        int bi = 0;
        if (xNeg) {
            b = &nb.xNeg;
            bi = bz;
        } else if (xPos) {
            b = &nb.xPos;
            bi = bz;
        } else if (zNeg) {
            b = &nb.zNeg;
            bi = bx;
        } else {
            b = &nb.zPos;
            bi = bx;
        }
        if (!b->valid) return {CellKind::Solid, 0};
        return classify(b->types[bi][by], b->waterBorder[bi][by]);
    }
    size_t i = static_cast<size_t>(bx) * CHUNK_HEIGHT * CHUNK_SIZE + by * CHUNK_SIZE + bz;
    uint8_t raw = waterLevels ? waterLevels[i] : 0;
    return classify(blocks[i].getType(), raw);
}

static void computeWaterTopCornersBorder(const Cube* blocks, const uint8_t* waterLevels, int bx, int by, int bz,
                                         int uSign, int vSign, float out[4], const NeighborBorders& nb,
                                         block_type fluid) {
    computeWaterTopCornersT(bx, by, bz, uSign, vSign, out,
                            [&](int x, int y, int z) { return sampleBorder(blocks, waterLevels, nb, x, y, z, fluid); });
}

// Side-face corner indices into topCorners[4] computed with uSign=1, vSign=-1.
// Corners: 0=(-X,+Z), 1=(-X,-Z), 2=(+X,-Z), 3=(+X,+Z).
// [face][0] and [face][1] are the two corners on this face's edge.
static constexpr int SIDE_TOP_IDX[4][2] = {{0, 3}, {2, 1}, {1, 0}, {3, 2}};

MeshData buildMeshFromData(Cube* blocks, uint8_t* light, uint8_t* waterLevels, int maxSolidY, int chunkX, int chunkZ,
                           const NeighborBorders& nb) {
    ZoneScopedN("buildMeshFromData");
    int opaqueH = std::min(maxSolidY + 2, (int)CHUNK_HEIGHT);
    constexpr int OX = CHUNK_SIZE + 2, OZ = CHUNK_SIZE + 2;
    std::vector<uint8_t> opaq(static_cast<size_t>(OX) * opaqueH * OZ, 0);
    auto oIdx = [&](int x, int y, int z) -> int { return (x + 1) * opaqueH * OZ + y * OZ + (z + 1); };

    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int y = 0; y < opaqueH; y++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                block_type t = blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].getType();
                opaq[oIdx(x, y, z)] = isAOOccluder(t) ? 1 : 0;
            }

    for (int y = 0; y < opaqueH; y++)
        for (int z = 0; z < CHUNK_SIZE; z++) {
            if (nb.xNeg.valid) opaq[oIdx(-1, y, z)] = isAOOccluder(nb.xNeg.types[z][y]) ? 1 : 0;
            if (nb.xPos.valid) opaq[oIdx(CHUNK_SIZE, y, z)] = isAOOccluder(nb.xPos.types[z][y]) ? 1 : 0;
        }
    for (int y = 0; y < opaqueH; y++)
        for (int x = 0; x < CHUNK_SIZE; x++) {
            if (nb.zNeg.valid) opaq[oIdx(x, y, -1)] = isAOOccluder(nb.zNeg.types[x][y]) ? 1 : 0;
            if (nb.zPos.valid) opaq[oIdx(x, y, CHUNK_SIZE)] = isAOOccluder(nb.zPos.types[x][y]) ? 1 : 0;
        }

    // Missing neighbors treated as solid — suppresses border faces until neighbor loads.
    auto getTypeCross = [&](int i, int j, int k) -> block_type {
        if (j < 0 || j >= CHUNK_HEIGHT) return AIR;
        if (i >= 0 && i < CHUNK_SIZE && k >= 0 && k < CHUNK_SIZE)
            return blocks[i * CHUNK_HEIGHT * CHUNK_SIZE + j * CHUNK_SIZE + k].getType();
        if (i < 0) return nb.xNeg.valid ? nb.xNeg.types[k][j] : STONE;
        if (i >= CHUNK_SIZE) return nb.xPos.valid ? nb.xPos.types[k][j] : STONE;
        if (k < 0) return nb.zNeg.valid ? nb.zNeg.types[i][j] : STONE;
        if (k >= CHUNK_SIZE) return nb.zPos.valid ? nb.zPos.types[i][j] : STONE;
        return STONE;
    };

    struct FaceDef {
        int d, u, v;
        int d_sign, u_sign, v_sign;
    };
    static constexpr FaceDef FACE_DEFS[6] = {
        {2, 0, 1, 1, 1, 1},  {2, 0, 1, -1, -1, 1}, {0, 2, 1, -1, 1, 1},
        {0, 2, 1, 1, -1, 1}, {1, 0, 2, 1, 1, -1},  {1, 0, 2, -1, 1, 1},
    };
    static constexpr int DIM[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
    static constexpr int MAX_DIM = CHUNK_HEIGHT > CHUNK_SIZE ? CHUNK_HEIGHT : CHUNK_SIZE;

    constexpr size_t MAX_FACES = static_cast<size_t>(CHUNK_SIZE) * CHUNK_HEIGHT * 4;
    std::vector<uint8_t> opaqueVerts, waterVerts;
    std::vector<unsigned int> opaqueIdx, waterIdx;
    opaqueVerts.reserve(MAX_FACES * 4 * BYTES_PER_VERT);
    opaqueIdx.reserve(MAX_FACES * 6);
    waterVerts.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 4 * BYTES_PER_VERT);
    waterIdx.reserve(static_cast<size_t>(CHUNK_SIZE) * CHUNK_SIZE * 6);
    unsigned int opaqueBase = 0, waterBase = 0;

    auto slDirect = [light, &nb](int x, int y, int z) -> int {
        return unpackSky(sampleLightBorder(light, nb, x, y, z));
    };
    auto blDirect = [light, &nb](int x, int y, int z) -> int {
        return unpackBlock(sampleLightBorder(light, nb, x, y, z));
    };

    // Snapshot mesher options once — Settings may flip them mid-build.
    const bool useGreedy = g_greedyMeshing.load();
    const bool useFancyLeaves = g_fancyLeaves.load();

    // Greedy merge key for a visible face (see mergeKey in chunk.cpp for
    // the rationale): block type + the face's light + its AO, and only when
    // all four AO corners agree. -1 = emit this face on its own. Water: only
    // flat source-water tops merge, keyed by their flat height.
    auto mergeKey = [&](const FaceDef& fd, const int c[3], block_type bt) -> int {
        int waterBits = 0;
        if (hasFlag(bt, BF_LIQUID)) {
            if (fd.d != 1 || fd.d_sign != 1) return -1; // top faces only
            size_t wi = static_cast<size_t>(c[0]) * CHUNK_HEIGHT * CHUNK_SIZE +
                        static_cast<size_t>(c[1]) * CHUNK_SIZE + c[2];
            uint8_t raw = waterLevels ? waterLevels[wi] : 0;
            if (!waterIsSource(raw)) return -1;
            float corners[4];
            computeWaterTopCornersBorder(blocks, waterLevels, c[0], c[1], c[2], fd.u_sign, fd.v_sign, corners, nb, bt);
            for (int i = 1; i < 4; i++)
                if (corners[i] != corners[0]) return -1;
            waterBits = (17 + static_cast<int>(std::lround((corners[0] - c[1]) * 32.0f))) << 18;
        }
        int p[3] = {c[0], c[1], c[2]};
        p[fd.d] += fd.d_sign;
        int aoFirst = -1;
        for (int su : {-1, 1}) {
            for (int sv : {-1, 1}) {
                int a[3] = {p[0], p[1], p[2]}, b[3] = {p[0], p[1], p[2]};
                a[fd.u] += su;
                b[fd.v] += sv;
                int ab[3] = {a[0], a[1], a[2]};
                ab[fd.v] += sv;
                int s1 = (a[1] >= 0 && a[1] < opaqueH) ? opaq[oIdx(a[0], a[1], a[2])] : 0;
                int s2 = (b[1] >= 0 && b[1] < opaqueH) ? opaq[oIdx(b[0], b[1], b[2])] : 0;
                int cr = (ab[1] >= 0 && ab[1] < opaqueH) ? opaq[oIdx(ab[0], ab[1], ab[2])] : 0;
                int aoVal = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
                if (aoFirst < 0)
                    aoFirst = aoVal;
                else if (aoVal != aoFirst)
                    return -1;
            }
        }
        int sky = slDirect(p[0], p[1], p[2]);
        int blk = blDirect(p[0], p[1], p[2]);
        return static_cast<int>(bt) | (aoFirst << 8) | (sky << 10) | (blk << 14) | waterBits;
    };

    int mask[MAX_DIM][MAX_DIM]; // block type of each visible face, -1 = none
    int mkey[MAX_DIM][MAX_DIM]; // greedy merge key (mergeKey), -1 = don't merge
    const float worldOff[3] = {(float)(chunkX * CHUNK_SIZE), 0.0f, (float)(chunkZ * CHUNK_SIZE)};
    const int effDIM[3] = {CHUNK_SIZE, maxSolidY + 2, CHUNK_SIZE};

    // Cave culling needs per-section index subranges, so geometry is
    // emitted section by section — the Y axis (whether it's the slice
    // axis d or the in-plane axis v) is clamped to [yMin, yMax) exactly
    // like the online per-section builder in chunk.cpp. Quads therefore
    // never span a section boundary; the extra splits on tall faces are
    // the price of section-granular draws.
    SectionDrawRanges ranges[secvis::NUM_SECTIONS];
    std::vector<unsigned int> crossIdx;

    for (int sect = 0; sect < secvis::NUM_SECTIONS; sect++) {
        const int yMin = sect * 16;
        const int yMax = std::min(yMin + 16, (int)CHUNK_HEIGHT);
        auto& range = ranges[sect];
        range.opaqueStart = (uint32_t)opaqueIdx.size();
        range.crossStart = (uint32_t)crossIdx.size();
        range.waterStart = (uint32_t)waterIdx.size();

    for (int f = 0; f < 6; f++) {
        const FaceDef& fd = FACE_DEFS[f];
        const int d_dim = std::min(DIM[fd.d], effDIM[fd.d]);
        const int u_dim = std::min(DIM[fd.u], effDIM[fd.u]);
        const int v_dim = std::min(DIM[fd.v], effDIM[fd.v]);

        // Clamp the Y-related axis to the current section's range.
        const int d_start = (fd.d == 1) ? std::max(yMin, 0) : 0;
        const int d_end = (fd.d == 1) ? std::min(yMax, d_dim) : d_dim;
        const int v_start = (fd.v == 1) ? std::max(yMin, 0) : 0;
        const int v_end = (fd.v == 1) ? std::min(yMax, v_dim) : v_dim;

        for (int d = d_start; d < d_end; d++) {
            bool anyFace = false;
            for (int u = 0; u < u_dim; u++) {
                for (int v = v_start; v < v_end; v++) {
                    int c[3];
                    c[fd.d] = d;
                    c[fd.u] = u;
                    c[fd.v] = v;
                    block_type bt = blocks[c[0] * CHUNK_HEIGHT * CHUNK_SIZE + c[1] * CHUNK_SIZE + c[2]].getType();
                    // Cross-quad plants are emitted separately after the
                    // face loop — skip them here so they don't try to emit
                    // cube faces.
                    if (bt == AIR || hasFlag(bt, BF_CROSS)) {
                        mask[u][v] = -1;
                        mkey[u][v] = -1;
                        continue;
                    }
                    int nc[3] = {c[0], c[1], c[2]};
                    nc[fd.d] += fd.d_sign;
                    block_type nbType = getTypeCross(nc[0], nc[1], nc[2]);
                    bool show;
                    if (hasFlag(bt, BF_LIQUID)) {
                        // Against air, or a different liquid (lava next to water shows
                        // until it hardens).
                        show = (f != 5) && (nbType == AIR || (hasFlag(nbType, BF_LIQUID) && nbType != bt));
                    } else {
                        show = (nbType == AIR) || hasFlag(nbType, BF_CROSS) ||
                               (hasFlag(nbType, BF_LIQUID) && !hasFlag(bt, BF_LIQUID)) ||
                               (useFancyLeaves && (hasFlag(bt, BF_TRANSLUCENT) || hasFlag(nbType, BF_TRANSLUCENT)));
                    }
                    int val = show ? (int)bt : -1;
                    mask[u][v] = val;
                    mkey[u][v] = (show && useGreedy) ? mergeKey(fd, c, bt) : -1;
                    if (val != -1) anyFace = true;
                }
            }
            if (!anyFace) continue;

            for (int u = 0; u < u_dim; u++) {
                for (int v = v_start; v < v_end;) {
                    int bt = mask[u][v];
                    if (bt == -1) {
                        v++;
                        continue;
                    }

                    // Merge only faces with an identical key — same block,
                    // light and uniform AO — so the per-corner AO/light
                    // sampled below stays exact across the merged quad.
                    int h = 1, w = 1;
                    const int key = mkey[u][v];
                    if (useGreedy && key != -1) {
                        while (v + h < v_end && mkey[u][v + h] == key) h++;
                        w = 1;
                        while (u + w < u_dim) {
                            bool ok = true;
                            for (int dv = 0; dv < h && ok; dv++) ok = (mkey[u + w][v + dv] == key);
                            if (!ok) break;
                            w++;
                        }
                    }

                    float layer = (float)block_layers::layerForFace((block_type)bt, f);
                    float d_val = (float)d + fd.d_sign * 0.5f;

                    float waterY2[4] = {d_val, d_val, d_val, d_val};
                    const bool liquid = hasFlag((block_type)bt, BF_LIQUID);
                    if (liquid && f == 4) {
                        computeWaterTopCornersBorder(blocks, waterLevels, u, d, v, fd.u_sign, fd.v_sign, waterY2, nb,
                                                     (block_type)bt);
                    }

                    float u_lo = (float)u - 0.5f, u_hi = (float)(u + w) - 0.5f;
                    float v_lo = (float)v - 0.5f, v_hi = (float)(v + h) - 0.5f;
                    float u0 = fd.u_sign > 0 ? u_lo : u_hi;
                    float u1 = fd.u_sign > 0 ? u_hi : u_lo;
                    float v0 = fd.v_sign > 0 ? v_lo : v_hi;
                    float v1 = fd.v_sign > 0 ? v_hi : v_lo;

                    float vp[4][3];
                    bool isWT2 = (liquid && f == 4);

                    bool isWaterSide2 = (liquid && f != 4 && f != 5 && fd.v == 1);
                    float sideTopA2 = v1, sideTopB2 = v1;
                    if (isWaterSide2 && waterLevels) {
                        int bc[3];
                        bc[fd.d] = d;
                        bc[fd.u] = u;
                        bc[fd.v] = v;
                        float topY2[4];
                        computeWaterTopCornersBorder(blocks, waterLevels, bc[0], bc[1], bc[2], 1, -1, topY2, nb,
                                                     (block_type)bt);
                        sideTopA2 = topY2[SIDE_TOP_IDX[f][0]];
                        sideTopB2 = topY2[SIDE_TOP_IDX[f][1]];
                    }

                    vp[0][fd.d] = isWT2 ? waterY2[0] : d_val;
                    vp[0][fd.u] = u0;
                    vp[0][fd.v] = v0;
                    vp[1][fd.d] = isWT2 ? waterY2[1] : d_val;
                    vp[1][fd.u] = u0;
                    vp[1][fd.v] = isWaterSide2 ? sideTopA2 : v1;
                    vp[2][fd.d] = isWT2 ? waterY2[2] : d_val;
                    vp[2][fd.u] = u1;
                    vp[2][fd.v] = isWaterSide2 ? sideTopB2 : v1;
                    vp[3][fd.d] = isWT2 ? waterY2[3] : d_val;
                    vp[3][fd.u] = u1;
                    vp[3][fd.v] = v0;

                    const float uvs[4][2] = {{0.f, 0.f}, {0.f, (float)h}, {(float)w, (float)h}, {(float)w, 0.f}};

                    int skyLightVals[4], blockLightVals[4];
                    int bu[4], bv[4], cu[4], cv[4];
                    bu[0] = bu[1] = (fd.u_sign > 0) ? u : u + w - 1;
                    bu[2] = bu[3] = (fd.u_sign > 0) ? u + w - 1 : u;
                    bv[0] = bv[3] = (fd.v_sign > 0) ? v : v + h - 1;
                    bv[1] = bv[2] = (fd.v_sign > 0) ? v + h - 1 : v;
                    cu[0] = cu[1] = -fd.u_sign;
                    cu[2] = cu[3] = fd.u_sign;
                    cv[0] = cv[3] = -fd.v_sign;
                    cv[1] = cv[2] = fd.v_sign;

                    int ao[4];
                    for (int vi = 0; vi < 4; vi++) {
                        int bc[3];
                        bc[fd.d] = d;
                        bc[fd.u] = bu[vi];
                        bc[fd.v] = bv[vi];
                        int n[3] = {0, 0, 0};
                        n[fd.d] = fd.d_sign;
                        int su[3] = {0, 0, 0};
                        su[fd.u] = cu[vi];
                        int sv[3] = {0, 0, 0};
                        sv[fd.v] = cv[vi];

                        int px = bc[0] + n[0], py = bc[1] + n[1], pz = bc[2] + n[2];
                        int s1 = (py + su[1] >= 0 && py + su[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0], py + su[1], pz + su[2])]
                                     : 0;
                        int s2 = (py + sv[1] >= 0 && py + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + sv[0], py + sv[1], pz + sv[2])]
                                     : 0;
                        int cr = (py + su[1] + sv[1] >= 0 && py + su[1] + sv[1] < opaqueH)
                                     ? opaq[oIdx(px + su[0] + sv[0], py + su[1] + sv[1], pz + su[2] + sv[2])]
                                     : 0;
                        int aoVal = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
                        ao[vi] = aoVal;
                        skyLightVals[vi] = slDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                        blockLightVals[vi] = blDirect(bc[0] + n[0], bc[1] + n[1], bc[2] + n[2]);
                    }

                    // Both liquids go in the liquid pass (lava draws opaque there).
                    bool isWater = liquid;
                    auto& idx = isWater ? waterIdx : opaqueIdx;
                    unsigned int& base = isWater ? waterBase : opaqueBase;

                    if (isWater) {
                        size_t off = waterVerts.size();
                        waterVerts.resize(off + 4 * WATER_BYTES_PER_VERT);
                        WaterVertex* wdst = reinterpret_cast<WaterVertex*>(&waterVerts[off]);
                        for (int vi = 0; vi < 4; vi++) {
                            wdst->px = (vp[vi][0] + worldOff[0]) * 2.0f;
                            wdst->py = (vp[vi][1] + worldOff[1]) * 2.0f;
                            wdst->pz = (vp[vi][2] + worldOff[2]) * 2.0f;
                            wdst->u = (uint8_t)uvs[vi][0];
                            wdst->v = (uint8_t)uvs[vi][1];
                            wdst->normalIdx = (uint8_t)f;
                            wdst->texLayer = (uint8_t)layer;
                            wdst->ao = (uint8_t)ao[vi];
                            wdst->packedLight = (uint8_t)(skyLightVals[vi] * 16 + blockLightVals[vi]);
                            wdst++;
                        }
                    } else {
                        size_t off = opaqueVerts.size();
                        opaqueVerts.resize(off + 4 * BYTES_PER_VERT);
                        PackedVertex* dst = reinterpret_cast<PackedVertex*>(&opaqueVerts[off]);
                        for (int vi = 0; vi < 4; vi++) {
                            dst->px = (int16_t)((vp[vi][0] + worldOff[0]) * 2.0f);
                            dst->py = (int16_t)((vp[vi][1] + worldOff[1]) * 2.0f);
                            dst->pz = (int16_t)((vp[vi][2] + worldOff[2]) * 2.0f);
                            dst->u = (uint8_t)uvs[vi][0];
                            dst->v = (uint8_t)uvs[vi][1];
                            dst->normalIdx = (uint8_t)f;
                            dst->texLayer = (uint8_t)layer;
                            dst->ao = (uint8_t)ao[vi];
                            dst->packedLight = (uint8_t)(skyLightVals[vi] * 16 + blockLightVals[vi]);
                            dst++;
                        }
                    }
                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        idx.push_back(base);
                        idx.push_back(base + 1);
                        idx.push_back(base + 2);
                        idx.push_back(base + 2);
                        idx.push_back(base + 3);
                        idx.push_back(base);
                    } else {
                        idx.push_back(base + 1);
                        idx.push_back(base + 2);
                        idx.push_back(base + 3);
                        idx.push_back(base + 3);
                        idx.push_back(base);
                        idx.push_back(base + 1);
                    }
                    base += 4;

                    if (useGreedy) {
                        for (int du = 0; du < w; du++)
                            for (int dv = 0; dv < h; dv++) mask[u + du][v + dv] = mkey[u + du][v + dv] = -1;
                    }
                    v += h;
                }
            }
        }
    }

    // --- Cross-quad pass for plants (BF_CROSS), this section only ---
    // Each plant is rendered as two intersecting planes (X shape) made
    // double-sided by emitting each quad twice with opposite winding.
    // Texture cutout is handled in the fragment shader (alpha < 0.5 →
    // discard for plant layers).
    auto emitCross = [&](int lx, int ly, int lz, block_type bt) {
        float layer = (float)block_layers::layerForFace(bt, 0);
        int skyL = slDirect(lx, ly, lz);
        int blkL = blDirect(lx, ly, lz);
        uint8_t packedLight = (uint8_t)(skyL * 16 + blkL);
        float wx = (float)(lx + chunkX * CHUNK_SIZE);
        float wz = (float)(lz + chunkZ * CHUNK_SIZE);
        float wy = (float)ly;
        // Each plane: 4 unique verts + 6 forward-winding indices. Plants
        // render in a second draw call with GL_CULL_FACE disabled so both
        // sides of each quad are visible, no reverse-winding dupes needed.
        auto emitPlane = [&](float p[4][3]) {
            size_t off = opaqueVerts.size();
            opaqueVerts.resize(off + 4 * BYTES_PER_VERT);
            PackedVertex* dst = reinterpret_cast<PackedVertex*>(&opaqueVerts[off]);
            constexpr uint8_t uv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
            for (int vi = 0; vi < 4; ++vi) {
                dst->px = (int16_t)(p[vi][0] * 2.0f);
                dst->py = (int16_t)(p[vi][1] * 2.0f);
                dst->pz = (int16_t)(p[vi][2] * 2.0f);
                dst->u = uv[vi][0];
                dst->v = uv[vi][1];
                dst->normalIdx = 4; // +Y — gives the plant full face-brightness
                dst->texLayer = (uint8_t)layer;
                dst->ao = 3; // AO_CURVE[3] = 1.0 (full bright)
                dst->packedLight = packedLight;
                dst++;
            }
            unsigned int b = opaqueBase;
            crossIdx.push_back(b);
            crossIdx.push_back(b + 1);
            crossIdx.push_back(b + 2);
            crossIdx.push_back(b + 2);
            crossIdx.push_back(b + 3);
            crossIdx.push_back(b);
            opaqueBase += 4;
        };
        float y0 = wy - 0.5f, y1 = wy + 0.5f;
        float planeA[4][3] = {{wx - 0.5f, y0, wz - 0.5f},
                              {wx - 0.5f, y1, wz - 0.5f},
                              {wx + 0.5f, y1, wz + 0.5f},
                              {wx + 0.5f, y0, wz + 0.5f}};
        float planeB[4][3] = {{wx - 0.5f, y0, wz + 0.5f},
                              {wx - 0.5f, y1, wz + 0.5f},
                              {wx + 0.5f, y1, wz - 0.5f},
                              {wx + 0.5f, y0, wz - 0.5f}};
        emitPlane(planeA);
        emitPlane(planeB);
    };

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = yMin; y < std::min(yMax, opaqueH); ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                block_type t = blocks[x * CHUNK_HEIGHT * CHUNK_SIZE + y * CHUNK_SIZE + z].getType();
                if (hasFlag(t, BF_CROSS)) emitCross(x, y, z, t);
            }
        }
    }

    range.opaqueCount = (uint32_t)opaqueIdx.size() - range.opaqueStart;
    range.crossCount = (uint32_t)crossIdx.size() - range.crossStart;
    range.waterCount = (uint32_t)waterIdx.size() - range.waterStart;
    } // end per-section loop

    MeshData result;
    result.verts = std::move(opaqueVerts);
    result.waterVerts = std::move(waterVerts);
    result.opaqueIdx = std::move(opaqueIdx);
    result.crossIdx = std::move(crossIdx);
    result.waterIdx = std::move(waterIdx);
    for (int s = 0; s < secvis::NUM_SECTIONS; s++) {
        result.ranges[s] = ranges[s];
        result.sectionConn[s] = secvis::computeSectionConnectivity(blocks, s);
    }
    result.ready = true;
    return result;
}
