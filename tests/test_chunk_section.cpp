#include <gtest/gtest.h>
#include "chunk_section.h"

TEST(ChunkSectionTest, DefaultIsEmpty) {
    ChunkSection sec;
    EXPECT_TRUE(sec.isEmpty());
    EXPECT_EQ(sec.getBlock(0, 0, 0), AIR);
    EXPECT_EQ(sec.getBlock(15, 15, 15), AIR);
}

TEST(ChunkSectionTest, SetAndGetBlock) {
    ChunkSection sec;
    sec.setBlock(5, 10, 3, STONE);
    EXPECT_EQ(sec.getBlock(5, 10, 3), STONE);
    EXPECT_EQ(sec.getBlock(0, 0, 0), AIR); // others unchanged
    EXPECT_FALSE(sec.isEmpty());
}

TEST(ChunkSectionTest, AllCorners) {
    ChunkSection sec;
    sec.setBlock(0, 0, 0, GRASS);
    sec.setBlock(15, 0, 0, DIRT);
    sec.setBlock(0, 15, 0, STONE);
    sec.setBlock(0, 0, 15, SAND);
    sec.setBlock(15, 15, 15, WOOD);
    EXPECT_EQ(sec.getBlock(0, 0, 0), GRASS);
    EXPECT_EQ(sec.getBlock(15, 0, 0), DIRT);
    EXPECT_EQ(sec.getBlock(0, 15, 0), STONE);
    EXPECT_EQ(sec.getBlock(0, 0, 15), SAND);
    EXPECT_EQ(sec.getBlock(15, 15, 15), WOOD);
}

TEST(ChunkSectionTest, PaletteGrows) {
    ChunkSection sec;
    // Add many block types to force palette growth
    block_type types[] = {GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE, WOOD, LEAVES, SNOW, GRAVEL, CACTUS};
    for (int i = 0; i < 13; i++) {
        sec.setBlock(i, 0, 0, types[i]);
    }
    // Verify all readable
    for (int i = 0; i < 13; i++) {
        EXPECT_EQ(sec.getBlock(i, 0, 0), types[i]) << "Failed at index " << i;
    }
    // Should need 4 bits (14 types including AIR = index 0..13)
    EXPECT_EQ(sec.getPalette().size(), 14u); // AIR + 13 types
    EXPECT_EQ(sec.getBitsPerBlock(), 4);
}

TEST(ChunkSectionTest, SingleTypeSectionUses1Bit) {
    ChunkSection sec;
    // Fill entirely with stone
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++)
                sec.setBlock(x, y, z, STONE);
    EXPECT_EQ(sec.getPalette().size(), 2u); // AIR + STONE
    EXPECT_EQ(sec.getBitsPerBlock(), 1);
}

TEST(ChunkSectionTest, CompressDecompressRoundTrip) {
    Cube source[ChunkSection::VOLUME];
    // Fill with a pattern
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++) {
                int i = x * 256 + y * 16 + z;
                if (y < 4) source[i].setType(STONE);
                else if (y < 8) source[i].setType(DIRT);
                else if (y == 8) source[i].setType(GRASS);
                else source[i].setType(AIR);
            }

    ChunkSection sec;
    sec.compress(source);

    Cube output[ChunkSection::VOLUME];
    sec.decompress(output);

    for (int i = 0; i < ChunkSection::VOLUME; i++) {
        EXPECT_EQ(output[i].getType(), source[i].getType()) << "Mismatch at index " << i;
    }
}

TEST(ChunkSectionTest, CompressAllAirIsEmpty) {
    Cube source[ChunkSection::VOLUME];
    for (int i = 0; i < ChunkSection::VOLUME; i++) source[i].setType(AIR);

    ChunkSection sec;
    sec.compress(source);
    EXPECT_TRUE(sec.isEmpty());
}

TEST(ChunkSectionTest, MemorySmall) {
    ChunkSection sec;
    // Single-type: palette(1) + data(64 u64s for 1-bit packing) = ~513 bytes
    EXPECT_LT(sec.memoryUsage(), 1024u);
}

TEST(ChunkSectionTest, SetBlockOverwrite) {
    ChunkSection sec;
    sec.setBlock(5, 5, 5, STONE);
    EXPECT_EQ(sec.getBlock(5, 5, 5), STONE);
    sec.setBlock(5, 5, 5, DIRT);
    EXPECT_EQ(sec.getBlock(5, 5, 5), DIRT);
    sec.setBlock(5, 5, 5, AIR);
    EXPECT_EQ(sec.getBlock(5, 5, 5), AIR);
}
