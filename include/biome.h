#pragma once

// Biome registry. Each Biome has one BiomeDef, defined in its own file
// src/biomes/<name>.cpp (the BIOME_DEFS table in src/biome.cpp points at
// them), so a biome's look and vegetation live in one place:
//
//   surface / subsurface / shore blocks, tree density + tree shape,
//   ground cover, per-cell features (cactus...), grass + foliage tint.
//
// Which biome a column gets is decided by pickBiome (the climate table)
// from temperature and humidity; see biome.cpp.
//
// Generation is GL-free and deterministic: everything a BiomeDef hook
// decides must come from the hash bits and column data it is handed (never
// from blocks another chunk may have written), so a tree that straddles a
// chunk border comes out identical from both sides.

#include <cstdint>

#include <glm/glm.hpp>

#include "cube.h"

namespace biomegen {

// Largest canopy radius (blocks from the trunk) any tree may use. Chunks
// sample this many columns (+2) beyond their edge so canopies that cross a
// border are drawn on both sides.
inline constexpr int TREE_MAX_RADIUS = 5;

// One tree being grown. (i, k) is the trunk column in chunk-local
// coordinates; it may lie outside the chunk (a neighbour's tree whose
// canopy reaches in): writes outside the chunk are dropped.
struct TreeSite {
    int i, k;
    int surface;   // y of the ground block under the trunk
    uint64_t bits; // free hash entropy for this tree (size, variant...)
    int wx, wz;    // world column of the trunk (for groves / patches)
    uint64_t seed; // world seed

    // Leaves / anything soft: written only into AIR (order-independent).
    void leaf(int di, int y, int dk, block_type t) const;
    // Logs: overwrite whatever is there (a trunk wins over a neighbour's
    // leaves), relative to the trunk column.
    void log(int di, int y, int dk, block_type t) const;
    // A vine chain hanging from (di, y, dk) down: VINE cells covering
    // `sides` (vine.h bits) in up to `len` cells, stopping at the first cell
    // that isn't air (or a vine, which gains the sides) and always leaving
    // at least one free cell above whatever is below (ground, water, a
    // plant). Reads only that column, so it comes out the same from both
    // chunks of a border. Returns how many cells it wrote.
    int vine(int di, int y, int dk, uint8_t sides, int len) const;
    // Marks the columns within `radius` of the trunk as shaded, so ground
    // cover and features skip them. Call once the tree is committed.
    void claim(int radius) const;
    // True if a tree reaching up to y = top stays under the world ceiling.
    bool fits(int top) const;

    // Set by terrain_fill.cpp.
    void* impl = nullptr;
};

// A per-cell feature candidate (cactus, boulder, lily pad...): one per
// TREE_CELL² world cell, always inside the chunk.
struct FeatureSite {
    int i, k;          // chunk-local column
    int surface;       // y of its surface block
    block_type ground; // the block at `surface` as generated (after shores)
    int seaLevel;
    uint64_t bits;     // free hash entropy for this cell
    int wx, wz;        // world column
    uint64_t seed;     // world seed

    block_type at(int y) const;                  // block in this column
    block_type atNear(int di, int y, int dk) const; // nearby column (AIR outside the chunk)
    void set(int y, block_type t) const;         // write into AIR only
    void setAt(int di, int y, int dk, block_type t) const; // nearby column, AIR only, in chunk
    // Overwrite (pools, ice, boulders...). Only inside this chunk, so keep
    // anything that must match across a border to a single column, or
    // accept that features stop at chunk edges.
    void replace(int di, int y, int dk, block_type t) const;
    void claim() const;                          // shade this column (ground cover skips it)
    // Biome of a nearby column (chunk-local offset from this site), so a
    // feature can keep to its own biome. Valid within TREE_MAX_RADIUS + 2
    // columns outside the chunk (terrain_fill's sampled margin; clamped beyond).
    Biome biomeAt(int di, int dk) const;

    void* impl = nullptr;
};

// One open, unshaded column above sea level that may get ground cover.
struct GroundSite {
    block_type ground; // its surface block
    uint32_t roll;     // uniform 0..9999, per column
    int wx, wz;        // world column (for patches: hash a coarser cell)
    uint64_t seed;     // world seed
    // The column's own climate (TerrainGenerator::ColumnClimate: altitude-
    // cooled temperature, humidity), continuous across biome borders.
    double temperature = 0.5;
    double humidity = 0.5;
};

// Grass as the climate grows it, the base every biome's ground cover
// builds on so the amount of grass fades across borders instead of
// jumping: TALL_GRASS, DOUBLE_TALL_GRASS or AIR for this column.
// grassDensity(climate) × `amount` of the columns get grass, and
// tallGrassShare(climate) × `tallScale` of those (in ~7-block clumps) are
// the two-block kind. A biome keeps its character by modulating these
// (drifts, clearings, a shaded floor) with factors that average near 1.
block_type climateGrass(const GroundSite& g, double amount = 1.0, double tallScale = 1.0);
// A second uniform 0..1 per column, independent of `roll` and of the one
// climateGrass draws, for a biome's own picks (ferns...).
double groundRoll(const GroundSite& g, uint64_t salt);

} // namespace biomegen

struct BiomeDef {
    const char* name;

    // Column blocks. Surface rules (water floor, snow line, cliffs, high
    // rock) still override `surface` where they apply.
    block_type surface;
    block_type subsurface; // soil under the surface (3..5 blocks)
    block_type shore;      // land touching water at sea level

    // Trees: chance (0..1) that a TREE_CELL² cell grows one, and the shape.
    // `growTree` returns false if the tree doesn't fit (it then must not
    // have written anything). Null = no trees.
    float treeDensity;
    bool (*growTree)(const biomegen::TreeSite&);

    // Per-cell features after trees (cactus, boulders...). Null = none.
    void (*decorate)(const biomegen::FeatureSite&);

    // Ground cover for an open, unshaded column above sea level: the plant
    // to put on top of it, or AIR. A two-block plant's lower half
    // (DOUBLE_TALL_GRASS, LARGE_FERN; double_plant.h) gets its upper half
    // on top when that cell is free air under the world ceiling; otherwise
    // the short version (TALL_GRASS, FERN) grows instead.
    block_type (*groundCover)(const biomegen::GroundSite&);

    // Colour multipliers over the stock textures, (1,1,1) = as drawn
    // (the stock grass / oak leaves are a temperate green). Blended across
    // biome borders and applied in frag.shd to grass (top, the green part of
    // the side), tall grass, ferns, and oak leaves. Spruce and birch leaves
    // keep their own colours, as in Minecraft.
    glm::vec3 grassTint;
    glm::vec3 foliageTint;
};

const BiomeDef& biomeDef(Biome b);

// Climate inputs for one land column.
struct Climate {
    double temperature;  // 0..1, regional
    double humidity;     // 0..1
    double cooled;       // temperature after altitude cooling (snow caps)
    double relief;       // blocks of mountain relief at the column
    int heightAboveSea;  // surface height - sea level
};

// The climate table: which land biome a column gets (never OCEAN/BEACH,
// those come from continentalness first). Pure, so it is unit-tested.
Biome pickBiome(const Climate& c);

// Share of open grass columns that grow grass (0..1) under a climate
// (altitude-cooled temperature, humidity; both 0..1). Smooth everywhere:
// rises with humidity (sparse when dry, dense when lush), falls to 0 in the
// cold (snow) and nearly so where it's hot and dry (desert).
float grassDensity(double temperature, double humidity);
// Share of that grass that is two blocks tall: highest in warm, not-wet
// grassland (savanna, plains), low in the cold and in wet forests.
float tallGrassShare(double temperature, double humidity);

// A column's grass (foliage = false) or foliage colour multiplier: its
// biome's tint shifted by how the column's climate differs from the
// biome's typical one (drier / hotter = yellower, wetter = lusher bluer
// green, colder = paler blue). Equal to the biome's tint at its typical
// climate; ~±15% per channel across a biome's usual climate range, at most
// ~-45% / +80% at the extremes. blendColumnTints (biome_tint.h) then blurs it.
glm::vec3 climateTint(Biome b, double temperature, double humidity, bool foliage);
