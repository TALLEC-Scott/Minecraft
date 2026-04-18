#include "world_save.h"
#include "chunk.h"
#include "chunk_section.h"
#include "TerrainGenerator.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static constexpr uint32_t CHUNK_MAGIC = 0x4D434348; // "MCCH"
static constexpr uint16_t CHUNK_VERSION = 1;

WorldSave::WorldSave(const std::string& savePath) : basePath(savePath), chunksPath(savePath + "/chunks") {
    scanSavedChunks();
}

void WorldSave::scanSavedChunks() {
    if (!std::filesystem::exists(chunksPath)) return;
    for (auto& entry : std::filesystem::directory_iterator(chunksPath)) {
        auto name = entry.path().stem().string(); // "c.5.-3"
        if (name.size() < 4 || name[0] != 'c' || name[1] != '.') continue;
        auto dot = name.find('.', 2);
        if (dot == std::string::npos) continue;
        int x = std::stoi(name.substr(2, dot - 2));
        int z = std::stoi(name.substr(dot + 1));
        savedChunks.insert({x, z});
    }
}

void WorldSave::ensureDirectories() {
    std::filesystem::create_directories(chunksPath);
}

std::string WorldSave::chunkFilePath(int x, int z) const {
    return chunksPath + "/c." + std::to_string(x) + "." + std::to_string(z) + ".dat";
}

// --- Level data (text key=value) ---

void WorldSave::saveLevelData(unsigned int seed, const PlayerSaveData& player) {
    ensureDirectories();
    std::ofstream f(basePath + "/level.dat");
    if (!f.is_open()) return;
    f << "seed=" << seed << "\n";
    f << "posX=" << player.position.x << "\n";
    f << "posY=" << player.position.y << "\n";
    f << "posZ=" << player.position.z << "\n";
    f << "yaw=" << player.yaw << "\n";
    f << "pitch=" << player.pitch << "\n";
    f << "walkMode=" << (player.walkMode ? 1 : 0) << "\n";
    f << "selectedSlot=" << player.selectedSlot << "\n";
    f << "hotbar=";
    for (int i = 0; i < Player::HOTBAR_SIZE; i++) {
        if (i > 0) f << ",";
        f << (int)player.hotbar[i];
    }
    f << "\n";
}

bool WorldSave::loadLevelData(unsigned int& seed, PlayerSaveData& player) {
    std::ifstream f(basePath + "/level.dat");
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "seed")
            seed = (unsigned int)std::stoul(val);
        else if (key == "posX")
            player.position.x = std::stof(val);
        else if (key == "posY")
            player.position.y = std::stof(val);
        else if (key == "posZ")
            player.position.z = std::stof(val);
        else if (key == "yaw")
            player.yaw = std::stof(val);
        else if (key == "pitch")
            player.pitch = std::stof(val);
        else if (key == "walkMode")
            player.walkMode = (std::stoi(val) != 0);
        else if (key == "selectedSlot")
            player.selectedSlot = std::clamp(std::stoi(val), 0, Player::HOTBAR_SIZE - 1);
        else if (key == "hotbar") {
            std::istringstream iss(val);
            std::string token;
            int i = 0;
            while (std::getline(iss, token, ',') && i < Player::HOTBAR_SIZE) {
                player.hotbar[i++] = static_cast<block_type>(std::stoi(token));
            }
        }
    }
    return true;
}

// --- Chunk binary I/O ---

bool WorldSave::chunkExists(int chunkX, int chunkZ) const {
    return savedChunks.count({chunkX, chunkZ}) > 0;
}

template <typename T> static void writeVal(std::ofstream& f, T v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
template <typename T> static bool readVal(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(v));
    return f.good();
}

void WorldSave::saveChunk(const Chunk& chunk) {
    ensureDirectories();
    std::ofstream f(chunkFilePath(chunk.chunkX, chunk.chunkY), std::ios::binary);
    if (!f.is_open()) return;
    savedChunks.insert({chunk.chunkX, chunk.chunkY});

    // Header
    writeVal<uint32_t>(f, CHUNK_MAGIC);
    writeVal<uint16_t>(f, CHUNK_VERSION);
    writeVal<int16_t>(f, 0); // reserved

    // Coordinates
    writeVal<int32_t>(f, chunk.chunkX);
    writeVal<int32_t>(f, chunk.chunkY); // chunkY is actually Z

    // Section mask
    uint8_t sectionMask = 0;
    for (int i = 0; i < NUM_SECTIONS; i++) {
        if (chunk.sections[i] && !chunk.sections[i]->isEmpty()) sectionMask |= (1 << i);
    }
    writeVal<uint8_t>(f, sectionMask);

    // Sections
    for (int i = 0; i < NUM_SECTIONS; i++) {
        if (!(sectionMask & (1 << i))) continue;
        const ChunkSection& sec = *chunk.sections[i];
        const auto& palette = sec.getPalette();
        uint8_t palSize = (uint8_t)palette.size();
        writeVal<uint8_t>(f, palSize);
        f.write(reinterpret_cast<const char*>(palette.data()), palSize);
        writeVal<uint8_t>(f, sec.getBitsPerBlock());
        const auto& packed = sec.getPackedData();
        uint32_t wordCount = (uint32_t)packed.size();
        writeVal<uint32_t>(f, wordCount);
        f.write(reinterpret_cast<const char*>(packed.data()), wordCount * sizeof(uint64_t));
    }

    // Water levels
    uint8_t hasWater = chunk.waterLevels ? 1 : 0;
    writeVal<uint8_t>(f, hasWater);
    if (hasWater) {
        constexpr size_t WATER_SIZE = (size_t)CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
        f.write(reinterpret_cast<const char*>(chunk.waterLevels.get()), WATER_SIZE);
    }
}

bool WorldSave::loadChunkData(int chunkX, int chunkZ, ChunkData& out, TerrainGenerator& terrain) {
    std::ifstream f(chunkFilePath(chunkX, chunkZ), std::ios::binary);
    if (!f.is_open()) return false;

    // Header
    uint32_t magic;
    uint16_t version;
    int16_t reserved;
    if (!readVal(f, magic) || magic != CHUNK_MAGIC) return false;
    if (!readVal(f, version) || version != CHUNK_VERSION) return false;
    readVal(f, reserved);

    // Coordinates
    int32_t cx, cz;
    readVal(f, cx);
    readVal(f, cz);
    out.chunkX = cx;
    out.chunkZ = cz;

    // Section mask
    uint8_t sectionMask;
    readVal(f, sectionMask);

    // Sections
    for (int i = 0; i < NUM_SECTIONS; i++) {
        if (!(sectionMask & (1 << i))) {
            out.sections[i].reset();
            continue;
        }
        uint8_t palSize;
        readVal(f, palSize);
        std::vector<uint8_t> palette(palSize);
        f.read(reinterpret_cast<char*>(palette.data()), palSize);

        uint8_t bitsPerBlock;
        readVal(f, bitsPerBlock);

        uint32_t wordCount;
        readVal(f, wordCount);
        std::vector<uint64_t> packed(wordCount);
        f.read(reinterpret_cast<char*>(packed.data()), wordCount * sizeof(uint64_t));

        out.sections[i] = std::make_unique<ChunkSection>();
        out.sections[i]->loadPacked(std::move(palette), bitsPerBlock, std::move(packed));
    }

    if (!f.good()) return false;

    // Water levels
    uint8_t hasWater;
    readVal(f, hasWater);
    if (hasWater) {
        constexpr size_t WATER_SIZE = (size_t)CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
        out.waterLevels = std::shared_ptr<uint8_t[]>(new uint8_t[WATER_SIZE]());
        f.read(reinterpret_cast<char*>(out.waterLevels.get()), WATER_SIZE);
    }

    // Recompute heights and biomes from terrain generator
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int gx = cx * CHUNK_SIZE + x;
            int gz = cz * CHUNK_SIZE + z;
            Biome b;
            out.heights[x][z] = terrain.getHeightAndBiome(gx, gz, b);
            out.biomes[x][z] = b;
        }
    }

    // Recompute maxSolidY by scanning sections
    out.maxSolidY = 0;
    for (int s = NUM_SECTIONS - 1; s >= 0; s--) {
        if (!out.sections[s]) continue;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int ly = 15; ly >= 0; ly--)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    if (out.sections[s]->getBlock(x, ly, z) != AIR) {
                        int y = s * 16 + ly;
                        if (y > out.maxSolidY) out.maxSolidY = y;
                    }
                }
    }

    // Recompute sky light: decompress sections to flat, compute, store back
    constexpr size_t FLAT_SIZE = (size_t)CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
    auto flatBlocks = std::make_unique<Cube[]>(FLAT_SIZE);
    for (int s = 0; s < NUM_SECTIONS; s++) {
        if (!out.sections[s]) continue;
        int baseY = s * 16;
        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int ly = 0; ly < 16; ly++)
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    int y = baseY + ly;
                    size_t idx = (size_t)x * CHUNK_HEIGHT * CHUNK_SIZE + (size_t)y * CHUNK_SIZE + z;
                    flatBlocks[idx].setType(out.sections[s]->getBlock(x, ly, z));
                }
    }

    out.skyLight = std::shared_ptr<uint8_t[]>(new uint8_t[FLAT_SIZE]());
    computeSkyLightData(flatBlocks.get(), out.skyLight.get(), out.maxSolidY);

    return f.good();
}

void WorldSave::mountPersistentStorage() {
    // On Emscripten, IDBFS is mounted in shell.html's preRun hook
    // (with addRunDependency to block main() until sync completes).
    // On desktop, saves/ is a regular directory — nothing to mount.
}

void WorldSave::syncToDisk() {
#ifdef __EMSCRIPTEN__
    EM_ASM(FS.syncfs(
        false, function(err) {
            if (err) console.error('IDBFS sync error:', err);
        }););
#endif
}
