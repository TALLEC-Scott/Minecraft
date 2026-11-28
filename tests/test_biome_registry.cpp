// The biome registry (biome.h): every Biome has a sane BiomeDef, the
// climate table places each land biome, and biome colours blend.
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "biome.h"
#include "biome_tint.h"

TEST(BiomeRegistryTest, EveryBiomeHasANamedDef) {
    std::set<std::string> names;
    for (int i = 0; i < BIOME_COUNT; i++) {
        const BiomeDef& d = biomeDef(static_cast<Biome>(i));
        ASSERT_NE(d.name, nullptr);
        EXPECT_TRUE(names.insert(d.name).second) << "duplicate name " << d.name;
        if (d.treeDensity > 0.0f) {
            EXPECT_NE(d.growTree, nullptr) << d.name << " has trees but no shape";
        }
        for (int c = 0; c < 3; c++) {
            EXPECT_GT(d.grassTint[c], 0.0f) << d.name;
            EXPECT_LT(d.grassTint[c], 2.0f) << d.name << ": tints are stored ×128 in a byte";
            EXPECT_GT(d.foliageTint[c], 0.0f) << d.name;
            EXPECT_LT(d.foliageTint[c], 2.0f) << d.name;
        }
    }
}

TEST(BiomeRegistryTest, ClimateTableReachesEveryLandBiome) {
    std::set<Biome> seen;
    for (double t = 0.0; t <= 1.0; t += 0.01)
        for (double h = 0.0; h <= 1.0; h += 0.01)
            for (int above : {2, 20})
                seen.insert(pickBiome(Climate{t, h, t, above > 10 ? 20.0 : 0.0, above}));
    for (int i = 0; i < BIOME_COUNT; i++) {
        auto b = static_cast<Biome>(i);
        if (b == BIOME_OCEAN || b == BIOME_BEACH) {
            EXPECT_FALSE(seen.count(b)) << "ocean / beach come from continentalness, not climate";
            continue;
        }
        EXPECT_TRUE(seen.count(b)) << biomeDef(b).name << " is unreachable in the climate table";
    }
}

TEST(BiomeRegistryTest, ColdHighGroundIsTundraAndWetHillsArentSwamp) {
    EXPECT_EQ(pickBiome(Climate{0.5, 0.5, 0.1, 30.0, 40}), BIOME_TUNDRA) << "altitude-cooled cap";
    EXPECT_EQ(pickBiome(Climate{0.45, 0.9, 0.45, 0.0, 2}), BIOME_SWAMPLAND);
    EXPECT_NE(pickBiome(Climate{0.45, 0.9, 0.45, 20.0, 25}), BIOME_SWAMPLAND);
    EXPECT_EQ(pickBiome(Climate{0.9, 0.1, 0.9, 0.0, 3}), BIOME_DESERT);
    EXPECT_EQ(pickBiome(Climate{0.9, 0.1, 0.9, 30.0, 30}), BIOME_SAVANNA) << "no dunes on mountains";
}

TEST(BiomeRegistryTest, TintsBlendAcrossABorder) {
    // Plains for i < 8, desert from 8: colours fade over the blend radius
    // instead of jumping, and are pure far from the border.
    BiomeTints tints{};
    blendBiomeTints([](int i, int) { return i < 8 ? BIOME_PLAINS : BIOME_DESERT; }, tints);
    const uint8_t plainsR = encodeTint(biomeDef(BIOME_PLAINS).grassTint.r);
    const uint8_t desertR = encodeTint(biomeDef(BIOME_DESERT).grassTint.r);
    EXPECT_EQ(tints.grass[0][5][0], plainsR) << "4+ columns from the border: pure plains";
    EXPECT_EQ(tints.grass[15][5][0], desertR) << "pure desert";
    for (int x = 1; x < CHUNK_SIZE; x++) EXPECT_GE(tints.grass[x][5][0], tints.grass[x - 1][5][0]) << "monotonic fade";
    EXPECT_GT(tints.grass[8][5][0], plainsR);
    EXPECT_LT(tints.grass[7][5][0], desertR);
}

TEST(BiomeRegistryTest, StockColourEncodesAs128) {
    EXPECT_EQ(encodeTint(1.0f), 128);
    EXPECT_EQ(encodeTint(0.0f), 0);
    EXPECT_EQ(encodeTint(5.0f), 255);
}

#include "screenshot_options.h"

TEST(ScreenshotOptionsTest, ParsesAStagedShot) {
    ScreenshotOptions o;
    EXPECT_FALSE(o.enabled());
    EXPECT_TRUE(o.parse("--screenshot", "shots"));
    EXPECT_TRUE(o.parse("--shot-at", "10.5,90,-20,45,-30"));
    EXPECT_TRUE(o.parse("--shot-time", "night"));
    EXPECT_TRUE(o.parse("--shot-frames", "40"));
    EXPECT_TRUE(o.enabled());
    EXPECT_TRUE(o.hasPose);
    EXPECT_FLOAT_EQ(o.eye.x, 10.5f);
    EXPECT_FLOAT_EQ(o.eye.z, -20.0f);
    EXPECT_FLOAT_EQ(o.pitch, -30.0f);
    EXPECT_TRUE(o.night);
    EXPECT_EQ(o.frames, 40);
}

TEST(ScreenshotOptionsTest, RejectsMalformedValues) {
    ScreenshotOptions o;
    EXPECT_FALSE(o.parse("--shot-at", "1,2,3"));
    EXPECT_FALSE(o.parse("--shot-at", "1,2,3,4,5x"));
    EXPECT_FALSE(o.parse("--shot-time", "dusk"));
    EXPECT_FALSE(o.parse("--shot-frames", "0"));
    EXPECT_FALSE(o.parse("--shot-frames", "12abc"));
    EXPECT_FALSE(o.parse("--screenshot", ""));
}
