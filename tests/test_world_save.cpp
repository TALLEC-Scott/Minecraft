#include <gtest/gtest.h>
#include "chunk_section.h"
#include "cube.h"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstring>

// --- ChunkSection loadPacked round-trip ---

TEST(WorldSaveTest, ChunkSectionPackedRoundTrip) {
    ChunkSection original;
    original.setBlock(0, 0, 0, STONE);
    original.setBlock(5, 10, 3, GRASS);
    original.setBlock(15, 15, 15, GLOWSTONE);
    original.setBlock(8, 4, 12, WOOD);

    // Extract packed representation
    auto palette = original.getPalette();
    uint8_t bits = original.getBitsPerBlock();
    auto data = original.getPackedData();

    // Load into a fresh section
    ChunkSection loaded;
    loaded.loadPacked(palette, bits, std::vector<uint64_t>(data));

    // Verify all blocks match
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++) {
                EXPECT_EQ(loaded.getBlock(x, y, z), original.getBlock(x, y, z))
                    << "Mismatch at (" << x << "," << y << "," << z << ")";
            }
}

TEST(WorldSaveTest, ChunkSectionPackedAllTypes) {
    ChunkSection original;
    block_type types[] = {GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND,
                          GLOWSTONE, WOOD, LEAVES, SNOW, GRAVEL, CACTUS};
    for (int i = 0; i < 13; i++) {
        original.setBlock(i, 0, 0, types[i]);
    }

    auto palette = original.getPalette();
    uint8_t bits = original.getBitsPerBlock();
    auto data = original.getPackedData();

    ChunkSection loaded;
    loaded.loadPacked(palette, bits, std::vector<uint64_t>(data));

    for (int i = 0; i < 13; i++) {
        EXPECT_EQ(loaded.getBlock(i, 0, 0), types[i]) << "Failed at index " << i;
    }
    EXPECT_EQ(loaded.getBlock(13, 0, 0), AIR);
}

TEST(WorldSaveTest, ChunkSectionPackedEmpty) {
    ChunkSection original; // all AIR

    auto palette = original.getPalette();
    uint8_t bits = original.getBitsPerBlock();
    auto data = original.getPackedData();

    ChunkSection loaded;
    loaded.loadPacked(palette, bits, std::vector<uint64_t>(data));

    EXPECT_TRUE(loaded.isEmpty());
    EXPECT_EQ(loaded.getBlock(8, 8, 8), AIR);
}

TEST(WorldSaveTest, ChunkSectionPackedFull) {
    // Fill entirely with one type, then overwrite some
    ChunkSection original;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++)
                original.setBlock(x, y, z, DIRT);
    original.setBlock(0, 0, 0, STONE);
    original.setBlock(15, 15, 15, GRASS);

    auto palette = original.getPalette();
    uint8_t bits = original.getBitsPerBlock();
    auto data = original.getPackedData();

    ChunkSection loaded;
    loaded.loadPacked(palette, bits, std::vector<uint64_t>(data));

    EXPECT_EQ(loaded.getBlock(0, 0, 0), STONE);
    EXPECT_EQ(loaded.getBlock(8, 8, 8), DIRT);
    EXPECT_EQ(loaded.getBlock(15, 15, 15), GRASS);
}

// level.dat parsing is covered by test_level_data.cpp (real readLevelData).

// --- Chunk binary format round-trip ---

static constexpr uint32_t CHUNK_MAGIC = 0x4D434348;
static constexpr uint16_t CHUNK_VERSION = 1;

template <typename T> static void writeVal(std::ofstream& f, T v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
template <typename T> static bool readVal(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(v));
    return f.good();
}

// Write a chunk file from sections (no GL dependencies)
static void writeChunkFile(const std::string& path, int cx, int cz,
                           ChunkSection* sections[8], bool hasWater,
                           const uint8_t* waterData) {
    std::ofstream f(path, std::ios::binary);
    writeVal<uint32_t>(f, CHUNK_MAGIC);
    writeVal<uint16_t>(f, CHUNK_VERSION);
    writeVal<int16_t>(f, 0);
    writeVal<int32_t>(f, cx);
    writeVal<int32_t>(f, cz);

    uint8_t mask = 0;
    for (int i = 0; i < 8; i++)
        if (sections[i] && !sections[i]->isEmpty()) mask |= (1 << i);
    writeVal<uint8_t>(f, mask);

    for (int i = 0; i < 8; i++) {
        if (!(mask & (1 << i))) continue;
        const auto& pal = sections[i]->getPalette();
        writeVal<uint8_t>(f, (uint8_t)pal.size());
        f.write(reinterpret_cast<const char*>(pal.data()), pal.size());
        writeVal<uint8_t>(f, sections[i]->getBitsPerBlock());
        const auto& data = sections[i]->getPackedData();
        writeVal<uint32_t>(f, (uint32_t)data.size());
        f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint64_t));
    }

    writeVal<uint8_t>(f, hasWater ? 1 : 0);
    if (hasWater) {
        f.write(reinterpret_cast<const char*>(waterData), CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE);
    }
}

// Read sections back from a chunk file
static bool readChunkFile(const std::string& path, int& cx, int& cz,
                          std::unique_ptr<ChunkSection> outSections[8],
                          bool& hasWater, std::vector<uint8_t>& waterOut) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    uint32_t magic; uint16_t ver; int16_t res;
    if (!readVal(f, magic) || magic != CHUNK_MAGIC) return false;
    if (!readVal(f, ver) || ver != CHUNK_VERSION) return false;
    readVal(f, res);
    readVal(f, cx); readVal(f, cz);

    uint8_t mask; readVal(f, mask);
    for (int i = 0; i < 8; i++) {
        if (!(mask & (1 << i))) { outSections[i].reset(); continue; }
        uint8_t palSize; readVal(f, palSize);
        std::vector<uint8_t> pal(palSize);
        f.read(reinterpret_cast<char*>(pal.data()), palSize);
        uint8_t bits; readVal(f, bits);
        uint32_t words; readVal(f, words);
        std::vector<uint64_t> data(words);
        f.read(reinterpret_cast<char*>(data.data()), words * sizeof(uint64_t));
        outSections[i] = std::make_unique<ChunkSection>();
        outSections[i]->loadPacked(std::move(pal), bits, std::move(data));
    }

    uint8_t hw; readVal(f, hw);
    hasWater = (hw != 0);
    if (hasWater) {
        waterOut.resize(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE);
        f.read(reinterpret_cast<char*>(waterOut.data()), waterOut.size());
    }
    return f.good();
}

class ChunkFileTest : public ::testing::Test {
  protected:
    std::string path;
    void SetUp() override {
        std::filesystem::create_directories("/tmp/minecraft_test_chunks");
        path = "/tmp/minecraft_test_chunks/c.5.-3.dat";
    }
    void TearDown() override { std::filesystem::remove_all("/tmp/minecraft_test_chunks"); }
};

TEST_F(ChunkFileTest, RoundTripBlocks) {
    ChunkSection sec0;
    sec0.setBlock(0, 0, 0, STONE);
    sec0.setBlock(5, 10, 3, GLOWSTONE);

    ChunkSection sec3;
    sec3.setBlock(8, 8, 8, WOOD);
    sec3.setBlock(15, 15, 15, LEAVES);

    ChunkSection* sections[8] = {};
    sections[0] = &sec0;
    sections[3] = &sec3;

    writeChunkFile(path, 5, -3, sections, false, nullptr);

    int cx, cz;
    std::unique_ptr<ChunkSection> loaded[8];
    bool hasWater;
    std::vector<uint8_t> water;
    ASSERT_TRUE(readChunkFile(path, cx, cz, loaded, hasWater, water));

    EXPECT_EQ(cx, 5);
    EXPECT_EQ(cz, -3);
    EXPECT_FALSE(hasWater);

    ASSERT_NE(loaded[0], nullptr);
    EXPECT_EQ(loaded[0]->getBlock(0, 0, 0), STONE);
    EXPECT_EQ(loaded[0]->getBlock(5, 10, 3), GLOWSTONE);
    EXPECT_EQ(loaded[0]->getBlock(1, 1, 1), AIR);

    EXPECT_EQ(loaded[1], nullptr);
    EXPECT_EQ(loaded[2], nullptr);

    ASSERT_NE(loaded[3], nullptr);
    EXPECT_EQ(loaded[3]->getBlock(8, 8, 8), WOOD);
    EXPECT_EQ(loaded[3]->getBlock(15, 15, 15), LEAVES);
}

TEST_F(ChunkFileTest, RoundTripWater) {
    ChunkSection sec0;
    sec0.setBlock(4, 4, 4, WATER);

    ChunkSection* sections[8] = {};
    sections[0] = &sec0;

    std::vector<uint8_t> waterLevels(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, 0);
    // Set a source at (4,4,4) and a flow at (5,4,4)
    size_t idx = 4 * CHUNK_HEIGHT * CHUNK_SIZE + 4 * CHUNK_SIZE + 4;
    waterLevels[idx] = 0; // source
    size_t idx2 = 5 * CHUNK_HEIGHT * CHUNK_SIZE + 4 * CHUNK_SIZE + 4;
    waterLevels[idx2] = 3; // flow level 3

    writeChunkFile(path, 0, 0, sections, true, waterLevels.data());

    int cx, cz;
    std::unique_ptr<ChunkSection> loaded[8];
    bool hasWater;
    std::vector<uint8_t> waterOut;
    ASSERT_TRUE(readChunkFile(path, cx, cz, loaded, hasWater, waterOut));

    EXPECT_TRUE(hasWater);
    ASSERT_EQ(waterOut.size(), waterLevels.size());
    EXPECT_EQ(waterOut[idx], 0);
    EXPECT_EQ(waterOut[idx2], 3);
}

TEST_F(ChunkFileTest, InvalidMagicFails) {
    {
        std::ofstream f(path, std::ios::binary);
        writeVal<uint32_t>(f, 0xDEADBEEF); // wrong magic
        writeVal<uint16_t>(f, 1);
        writeVal<int16_t>(f, 0);
    }
    int cx, cz;
    std::unique_ptr<ChunkSection> loaded[8];
    bool hasWater;
    std::vector<uint8_t> water;
    EXPECT_FALSE(readChunkFile(path, cx, cz, loaded, hasWater, water));
}

TEST_F(ChunkFileTest, MissingFileFails) {
    int cx, cz;
    std::unique_ptr<ChunkSection> loaded[8];
    bool hasWater;
    std::vector<uint8_t> water;
    EXPECT_FALSE(readChunkFile("/tmp/nonexistent.dat", cx, cz, loaded, hasWater, water));
}

TEST_F(ChunkFileTest, NegativeCoordinates) {
    ChunkSection sec0;
    sec0.setBlock(0, 0, 0, DIRT);

    ChunkSection* sections[8] = {};
    sections[0] = &sec0;

    writeChunkFile(path, -100, -200, sections, false, nullptr);

    int cx, cz;
    std::unique_ptr<ChunkSection> loaded[8];
    bool hasWater;
    std::vector<uint8_t> water;
    ASSERT_TRUE(readChunkFile(path, cx, cz, loaded, hasWater, water));
    EXPECT_EQ(cx, -100);
    EXPECT_EQ(cz, -200);
    ASSERT_NE(loaded[0], nullptr);
    EXPECT_EQ(loaded[0]->getBlock(0, 0, 0), DIRT);
}
