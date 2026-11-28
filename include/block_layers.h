#pragma once

// Pure block→texture-layer mapping — GL-free so it's usable from the
// offline mesh builder (chunk_mesh.cpp) and from tests. TextureArray
// re-exports the same constants/functions for the rendering path.

#include "cube.h"

namespace block_layers {

constexpr int GRASS_SIDE_LAYER = 11;
constexpr int SUN_LAYER = 12;
constexpr int SNOW_LAYER = 13;
constexpr int GRAVEL_LAYER = 14;
constexpr int CACTUS_LAYER = 15;
constexpr int CLOUD_LAYER = 16;
constexpr int SKIN_LAYER = 17;
constexpr int MOON_LAYER = 18;
constexpr int WOOD_TOP_LAYER = 19;
// TNT is block_type value 14, so its "side" layer = 20 (same as the enum index once we append).
// But block_type AIR=0...CACTUS=13, TNT=14. Layers 0..10 match block_type 0..10; 11..19 are
// auxiliary (grass_side, sun, etc). We want TNT side at layer 20, plus top/bottom.
constexpr int TNT_SIDE_LAYER = 20;
constexpr int TNT_TOP_LAYER = 21;
constexpr int TNT_BOTTOM_LAYER = 22;
constexpr int SMOKE_LAYER = 23;
constexpr int TALL_GRASS_LAYER = 24;
constexpr int DANDELION_LAYER = 25;
constexpr int POPPY_LAYER = 26;
constexpr int PLANKS_LAYER = 27;
constexpr int COBBLESTONE_LAYER = 28;
constexpr int CRAFTING_TABLE_TOP_LAYER = 29;
constexpr int CRAFTING_TABLE_SIDE_LAYER = 30;
constexpr int CRAFTING_TABLE_FRONT_LAYER = 31;
// Block-breaking crack overlay, stages 0..9 (frag.shd cuts these out).
constexpr int DESTROY_STAGE_FIRST_LAYER = 32;
constexpr int DESTROY_STAGE_COUNT = 10;
constexpr int LAVA_LAYER = DESTROY_STAGE_FIRST_LAYER + DESTROY_STAGE_COUNT; // 42 (frag.shd: opaque, glowing)
constexpr int OBSIDIAN_LAYER = LAVA_LAYER + 1;
constexpr int TORCH_LAYER = OBSIDIAN_LAYER + 1; // 44 (frag.shd: cutout like the plants)
constexpr int SPRUCE_LOG_LAYER = TORCH_LAYER + 1;
constexpr int SPRUCE_LOG_TOP_LAYER = SPRUCE_LOG_LAYER + 1;
constexpr int SPRUCE_LEAVES_LAYER = SPRUCE_LOG_TOP_LAYER + 1; // 47 (shaders: leaf cutout + sway)
constexpr int BIRCH_LOG_LAYER = SPRUCE_LEAVES_LAYER + 1;
constexpr int BIRCH_LOG_TOP_LAYER = BIRCH_LOG_LAYER + 1;
constexpr int BIRCH_LEAVES_LAYER = BIRCH_LOG_TOP_LAYER + 1; // 50 (shaders: leaf cutout + sway)
constexpr int FERN_LAYER = BIRCH_LEAVES_LAYER + 1;          // 51 (shaders: plant cutout + sway)
constexpr int IRON_ORE_LAYER = FERN_LAYER + 1;
constexpr int DIAMOND_ORE_LAYER = IRON_ORE_LAYER + 1;
constexpr int OXEYE_DAISY_LAYER = DIAMOND_ORE_LAYER + 1;
constexpr int CORNFLOWER_LAYER = OXEYE_DAISY_LAYER + 1;
// Oak bark turned 90°, for faces running along a sideways log (OAK_LOG_X/Z).
constexpr int WOOD_SIDEWAYS_LAYER = CORNFLOWER_LAYER + 1;
constexpr int PODZOL_TOP_LAYER = WOOD_SIDEWAYS_LAYER + 1;
constexpr int PODZOL_SIDE_LAYER = PODZOL_TOP_LAYER + 1;
constexpr int MOSSY_COBBLESTONE_LAYER = PODZOL_SIDE_LAYER + 1;
constexpr int ICE_LAYER = MOSSY_COBBLESTONE_LAYER + 1;
constexpr int SANDSTONE_LAYER = ICE_LAYER + 1; // side
constexpr int SANDSTONE_TOP_LAYER = SANDSTONE_LAYER + 1;
constexpr int SANDSTONE_BOTTOM_LAYER = SANDSTONE_TOP_LAYER + 1;
constexpr int DEAD_BUSH_LAYER = SANDSTONE_BOTTOM_LAYER + 1; // plant sprite (solid-pass cutout)
constexpr int ACACIA_LOG_LAYER = DEAD_BUSH_LAYER + 1;
constexpr int ACACIA_LOG_TOP_LAYER = ACACIA_LOG_LAYER + 1;
constexpr int ACACIA_LEAVES_LAYER = ACACIA_LOG_TOP_LAYER + 1; // 67 (shaders: leaf cutout + sway; colour baked in)
constexpr int JUNGLE_LOG_LAYER = ACACIA_LEAVES_LAYER + 1;
constexpr int JUNGLE_LOG_TOP_LAYER = JUNGLE_LOG_LAYER + 1;
constexpr int COARSE_DIRT_LAYER = JUNGLE_LOG_TOP_LAYER + 1;
// Autumn leaves keep their own baked colours (no foliage tint).
constexpr int RED_AUTUMN_LEAVES_LAYER = COARSE_DIRT_LAYER + 1;        // 71 (shaders: leaf cutout + sway)
constexpr int ORANGE_AUTUMN_LEAVES_LAYER = RED_AUTUMN_LEAVES_LAYER + 1; // 72 (shaders: leaf cutout + sway)
constexpr int ORANGE_TULIP_LAYER = ORANGE_AUTUMN_LEAVES_LAYER + 1;
constexpr int CLAY_LAYER = ORANGE_TULIP_LAYER + 1;
constexpr int BLUE_ORCHID_LAYER = CLAY_LAYER + 1;
// Two-block plants (double_plant.h), 76..79: shaders tint them like tall
// grass and sway them as one stalk (vert.shd / frag.shd test this range).
constexpr int DOUBLE_TALL_GRASS_BOTTOM_LAYER = BLUE_ORCHID_LAYER + 1;
constexpr int DOUBLE_TALL_GRASS_TOP_LAYER = DOUBLE_TALL_GRASS_BOTTOM_LAYER + 1;
constexpr int LARGE_FERN_BOTTOM_LAYER = DOUBLE_TALL_GRASS_TOP_LAYER + 1;
constexpr int LARGE_FERN_TOP_LAYER = LARGE_FERN_BOTTOM_LAYER + 1;
constexpr int NUM_LAYERS = LARGE_FERN_TOP_LAYER + 1;
// The shaders hard-code these; keep frag.shd / vert.shd in step if they move.
static_assert(SPRUCE_LEAVES_LAYER == 47 && BIRCH_LEAVES_LAYER == 50 && FERN_LAYER == 51 && ACACIA_LEAVES_LAYER == 67 &&
              RED_AUTUMN_LEAVES_LAYER == 71 && ORANGE_AUTUMN_LEAVES_LAYER == 72);
static_assert(DOUBLE_TALL_GRASS_BOTTOM_LAYER == 76 && LARGE_FERN_TOP_LAYER == 79, "shaders: two-block plants 76..79");

// Face-aware: grass uses different textures per face (top=grass,
// bottom=dirt, sides=grass_side). Wood is top/bottom vs sides. Other
// blocks with dedicated layers (snow, gravel, cactus) return theirs;
// everything else maps directly from block_type.
inline int layerForFace(block_type t, int face) {
    if (t == GRASS) {
        if (face == 4) return static_cast<int>(GRASS);
        if (face == 5) return static_cast<int>(DIRT);
        return GRASS_SIDE_LAYER;
    }
    if (t == WOOD) {
        if (face == 4 || face == 5) return WOOD_TOP_LAYER;
        return static_cast<int>(WOOD);
    }
    if (t == SNOW) return SNOW_LAYER;
    if (t == GRAVEL) return GRAVEL_LAYER;
    if (t == CACTUS) return CACTUS_LAYER;
    if (t == TNT) {
        if (face == 4) return TNT_TOP_LAYER;
        if (face == 5) return TNT_BOTTOM_LAYER;
        return TNT_SIDE_LAYER;
    }
    if (t == TALL_GRASS) return TALL_GRASS_LAYER;
    if (t == DANDELION) return DANDELION_LAYER;
    if (t == POPPY) return POPPY_LAYER;
    if (t == PLANKS) return PLANKS_LAYER;
    if (t == COBBLESTONE) return COBBLESTONE_LAYER;
    if (t == CRAFTING_TABLE) {
        if (face == 4) return CRAFTING_TABLE_TOP_LAYER;
        if (face == 5) return PLANKS_LAYER;
        // Two opposite sides show the tools, the other two the plain side.
        return (face == 0 || face == 1) ? CRAFTING_TABLE_FRONT_LAYER : CRAFTING_TABLE_SIDE_LAYER;
    }
    if (t == LAVA) return LAVA_LAYER;
    if (t == OBSIDIAN) return OBSIDIAN_LAYER;
    if (t == TORCH) return TORCH_LAYER;
    if (t == SPRUCE_LOG) return (face == 4 || face == 5) ? SPRUCE_LOG_TOP_LAYER : SPRUCE_LOG_LAYER;
    if (t == BIRCH_LOG) return (face == 4 || face == 5) ? BIRCH_LOG_TOP_LAYER : BIRCH_LOG_LAYER;
    if (t == SPRUCE_LEAVES) return SPRUCE_LEAVES_LAYER;
    if (t == BIRCH_LEAVES) return BIRCH_LEAVES_LAYER;
    if (t == FERN) return FERN_LAYER;
    if (t == IRON_ORE) return IRON_ORE_LAYER;
    if (t == DIAMOND_ORE) return DIAMOND_ORE_LAYER;
    if (t == OXEYE_DAISY) return OXEYE_DAISY_LAYER;
    if (t == CORNFLOWER) return CORNFLOWER_LAYER;
    // Sideways logs: rings on the two end faces, bark elsewhere. Faces 0/1
    // (±Z) and 4/5 (±Y) map u to X, faces 2/3 (±X) map u to Z, and ±Y map v
    // to Z; the bark's grain runs along v, so faces whose u runs along the
    // log take the turned bark.
    if (t == OAK_LOG_X) {
        if (face == 2 || face == 3) return WOOD_TOP_LAYER;
        return WOOD_SIDEWAYS_LAYER;
    }
    if (t == OAK_LOG_Z) {
        if (face == 0 || face == 1) return WOOD_TOP_LAYER;
        if (face == 2 || face == 3) return WOOD_SIDEWAYS_LAYER;
        return static_cast<int>(WOOD);
    }
    if (t == PODZOL) {
        if (face == 4) return PODZOL_TOP_LAYER;
        if (face == 5) return static_cast<int>(DIRT);
        return PODZOL_SIDE_LAYER;
    }
    if (t == MOSSY_COBBLESTONE) return MOSSY_COBBLESTONE_LAYER;
    if (t == ICE) return ICE_LAYER;
    if (t == SANDSTONE) {
        if (face == 4) return SANDSTONE_TOP_LAYER;
        if (face == 5) return SANDSTONE_BOTTOM_LAYER;
        return SANDSTONE_LAYER;
    }
    if (t == DEAD_BUSH) return DEAD_BUSH_LAYER;
    if (t == ACACIA_LOG) return (face == 4 || face == 5) ? ACACIA_LOG_TOP_LAYER : ACACIA_LOG_LAYER;
    if (t == ACACIA_LEAVES) return ACACIA_LEAVES_LAYER;
    if (t == JUNGLE_LOG) return (face == 4 || face == 5) ? JUNGLE_LOG_TOP_LAYER : JUNGLE_LOG_LAYER;
    if (t == COARSE_DIRT) return COARSE_DIRT_LAYER;
    if (t == RED_AUTUMN_LEAVES) return RED_AUTUMN_LEAVES_LAYER;
    if (t == ORANGE_AUTUMN_LEAVES) return ORANGE_AUTUMN_LEAVES_LAYER;
    if (t == ORANGE_TULIP) return ORANGE_TULIP_LAYER;
    if (t == CLAY) return CLAY_LAYER;
    if (t == BLUE_ORCHID) return BLUE_ORCHID_LAYER;
    // Two-block plants: the meshers ask for face 0 (the half's own sprite);
    // the item icon asks for face 4 and shows the top half, as in Minecraft.
    if (t == DOUBLE_TALL_GRASS) return face == 4 ? DOUBLE_TALL_GRASS_TOP_LAYER : DOUBLE_TALL_GRASS_BOTTOM_LAYER;
    if (t == DOUBLE_TALL_GRASS_TOP) return DOUBLE_TALL_GRASS_TOP_LAYER;
    if (t == LARGE_FERN) return face == 4 ? LARGE_FERN_TOP_LAYER : LARGE_FERN_BOTTOM_LAYER;
    if (t == LARGE_FERN_TOP) return LARGE_FERN_TOP_LAYER;
    return static_cast<int>(t);
}

inline int layerForType(block_type t) {
    if (t == TNT) return TNT_SIDE_LAYER;
    if (t == TALL_GRASS) return TALL_GRASS_LAYER;
    if (t == DANDELION) return DANDELION_LAYER;
    if (t == POPPY) return POPPY_LAYER;
    if (t == PLANKS) return PLANKS_LAYER;
    if (t == COBBLESTONE) return COBBLESTONE_LAYER;
    if (t == CRAFTING_TABLE) return CRAFTING_TABLE_SIDE_LAYER;
    if (t == LAVA) return LAVA_LAYER;
    if (t == OBSIDIAN) return OBSIDIAN_LAYER;
    if (t == TORCH) return TORCH_LAYER;
    if (t == SPRUCE_LOG) return SPRUCE_LOG_LAYER;
    if (t == SPRUCE_LEAVES) return SPRUCE_LEAVES_LAYER;
    if (t == BIRCH_LOG) return BIRCH_LOG_LAYER;
    if (t == BIRCH_LEAVES) return BIRCH_LEAVES_LAYER;
    if (t == FERN) return FERN_LAYER;
    if (t == IRON_ORE) return IRON_ORE_LAYER;
    if (t == DIAMOND_ORE) return DIAMOND_ORE_LAYER;
    if (t == OXEYE_DAISY) return OXEYE_DAISY_LAYER;
    if (t == CORNFLOWER) return CORNFLOWER_LAYER;
    if (t == OAK_LOG_X || t == OAK_LOG_Z) return static_cast<int>(WOOD);
    if (t == PODZOL) return PODZOL_SIDE_LAYER;
    if (t == MOSSY_COBBLESTONE) return MOSSY_COBBLESTONE_LAYER;
    if (t == ICE) return ICE_LAYER;
    if (t == SANDSTONE) return SANDSTONE_LAYER;
    if (t == DEAD_BUSH) return DEAD_BUSH_LAYER;
    if (t == ACACIA_LOG) return ACACIA_LOG_LAYER;
    if (t == ACACIA_LEAVES) return ACACIA_LEAVES_LAYER;
    if (t == JUNGLE_LOG) return JUNGLE_LOG_LAYER;
    if (t == COARSE_DIRT) return COARSE_DIRT_LAYER;
    if (t == RED_AUTUMN_LEAVES) return RED_AUTUMN_LEAVES_LAYER;
    if (t == ORANGE_AUTUMN_LEAVES) return ORANGE_AUTUMN_LEAVES_LAYER;
    if (t == ORANGE_TULIP) return ORANGE_TULIP_LAYER;
    if (t == CLAY) return CLAY_LAYER;
    if (t == BLUE_ORCHID) return BLUE_ORCHID_LAYER;
    // Held / dropped sprite of a two-block plant: its top half.
    if (t == DOUBLE_TALL_GRASS || t == DOUBLE_TALL_GRASS_TOP) return DOUBLE_TALL_GRASS_TOP_LAYER;
    if (t == LARGE_FERN || t == LARGE_FERN_TOP) return LARGE_FERN_TOP_LAYER;
    return static_cast<int>(t);
}

} // namespace block_layers
