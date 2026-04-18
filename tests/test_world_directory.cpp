#include <gtest/gtest.h>
#include "world_directory.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

namespace fs = std::filesystem;

static const char* ROOT = "/tmp/minecraft_test_worlds";

static void writeLevelDat(const fs::path& dir, const std::string& name, std::time_t t) {
    fs::create_directories(dir);
    std::ofstream f(dir / "level.dat");
    f << "seed=1\n";
    f << "worldName=" << name << "\n";
    f << "lastPlayed=" << static_cast<long long>(t) << "\n";
}

class WorldDirectoryTest : public ::testing::Test {
  protected:
    void SetUp() override { fs::remove_all(ROOT); fs::create_directories(ROOT); }
    void TearDown() override { fs::remove_all(ROOT); }
};

// --- sanitizeWorldName ---

TEST(WorldDirectorySanitize, StripsForbiddenChars) {
    EXPECT_EQ(sanitizeWorldName("hello/world"), "helloworld");
    EXPECT_EQ(sanitizeWorldName("a:b*c?"), "abc");
    EXPECT_EQ(sanitizeWorldName("w<o>r|l\"d"), "world");
}

TEST(WorldDirectorySanitize, TrimsWhitespace) {
    EXPECT_EQ(sanitizeWorldName("  spaced  "), "spaced");
    EXPECT_EQ(sanitizeWorldName("\tkeep inner\t"), "keep inner");
}

TEST(WorldDirectorySanitize, ClampsLength) {
    std::string big(200, 'x');
    auto out = sanitizeWorldName(big);
    EXPECT_LE(out.size(), 48u);
}

TEST(WorldDirectorySanitize, EmptyFallsBackToWorld) {
    EXPECT_EQ(sanitizeWorldName(""), "World");
    EXPECT_EQ(sanitizeWorldName("   "), "World");
    EXPECT_EQ(sanitizeWorldName("///"), "World");
}

// --- uniqueFolderName ---

TEST_F(WorldDirectoryTest, UniqueFolderReturnsBaseWhenFree) {
    EXPECT_EQ(uniqueFolderName("Fresh", ROOT), "Fresh");
}

TEST_F(WorldDirectoryTest, UniqueFolderAppendsCounter) {
    fs::create_directories(fs::path(ROOT) / "My World");
    EXPECT_EQ(uniqueFolderName("My World", ROOT), "My World (2)");
    fs::create_directories(fs::path(ROOT) / "My World (2)");
    EXPECT_EQ(uniqueFolderName("My World", ROOT), "My World (3)");
}

// --- resolveSeed ---

TEST(WorldDirectorySeed, EmptyRandomDiffers) {
    auto a = resolveSeed("");
    auto b = resolveSeed("");
    // Extremely unlikely to collide; std::random_device produces unique values.
    EXPECT_NE(a, b);
}

TEST(WorldDirectorySeed, NumericParsedDirectly) {
    EXPECT_EQ(resolveSeed("12345"), 12345u);
    EXPECT_EQ(resolveSeed("0"), 0u);
}

TEST(WorldDirectorySeed, StringDeterministic) {
    EXPECT_EQ(resolveSeed("hello"), resolveSeed("hello"));
    EXPECT_NE(resolveSeed("hello"), resolveSeed("world"));
}

TEST(WorldDirectorySeed, MixedHashed) {
    auto h = resolveSeed("123abc");
    EXPECT_EQ(h, resolveSeed("123abc"));
    // Should differ from the numeric-parse path
    EXPECT_NE(h, 123u);
}

TEST(WorldDirectorySeed, NumericOverflowFallsBackToHash) {
    // 10^20 doesn't fit in uint64 — stoull throws, we must not crash
    EXPECT_NO_THROW(resolveSeed("99999999999999999999"));
    EXPECT_EQ(resolveSeed("99999999999999999999"), resolveSeed("99999999999999999999"));
}

// --- listWorlds ---

TEST_F(WorldDirectoryTest, ListWorldsEmpty) {
    auto v = listWorlds(ROOT);
    EXPECT_TRUE(v.empty());
}

TEST_F(WorldDirectoryTest, ListWorldsIgnoresFoldersWithoutLevelDat) {
    fs::create_directories(fs::path(ROOT) / "NotAWorld");
    auto v = listWorlds(ROOT);
    EXPECT_TRUE(v.empty());
}

TEST_F(WorldDirectoryTest, ListWorldsReturnsSortedByLastPlayedDesc) {
    writeLevelDat(fs::path(ROOT) / "Alpha", "Alpha", 1000);
    writeLevelDat(fs::path(ROOT) / "Beta", "Beta", 3000);
    writeLevelDat(fs::path(ROOT) / "Gamma", "Gamma", 2000);
    auto v = listWorlds(ROOT);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].name, "Beta");
    EXPECT_EQ(v[1].name, "Gamma");
    EXPECT_EQ(v[2].name, "Alpha");
}

TEST_F(WorldDirectoryTest, ListWorldsFallsBackToFolderNameWhenMetaMissing) {
    // level.dat without worldName key
    fs::create_directories(fs::path(ROOT) / "Legacy");
    std::ofstream f(fs::path(ROOT) / "Legacy" / "level.dat");
    f << "seed=42\n";
    f.close();
    auto v = listWorlds(ROOT);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].name, "Legacy");
    EXPECT_EQ(v[0].folder, "Legacy");
}

// --- deleteWorld ---

TEST_F(WorldDirectoryTest, DeleteRemovesFolder) {
    writeLevelDat(fs::path(ROOT) / "Doomed", "Doomed", 1);
    fs::create_directories(fs::path(ROOT) / "Doomed" / "chunks");
    std::ofstream f(fs::path(ROOT) / "Doomed" / "chunks" / "c.0.0.dat");
    f << "data";
    f.close();
    EXPECT_TRUE(deleteWorld("Doomed", ROOT));
    EXPECT_FALSE(fs::exists(fs::path(ROOT) / "Doomed"));
}

TEST_F(WorldDirectoryTest, DeleteNonexistentIsNoop) {
    // Deleting a missing folder shouldn't throw; filesystem::remove_all
    // returns 0 with no error on missing paths.
    EXPECT_TRUE(deleteWorld("Nope", ROOT));
}

// --- renameWorld ---

TEST_F(WorldDirectoryTest, RenameMovesFolderAndUpdatesLevelDat) {
    writeLevelDat(fs::path(ROOT) / "OldName", "OldName", 100);
    fs::create_directories(fs::path(ROOT) / "OldName" / "chunks");
    std::ofstream(fs::path(ROOT) / "OldName" / "chunks" / "c.1.2.dat") << "x";

    std::string newFolder;
    ASSERT_TRUE(renameWorld("OldName", "Brand New", newFolder, ROOT));
    EXPECT_EQ(newFolder, "Brand New");
    EXPECT_FALSE(fs::exists(fs::path(ROOT) / "OldName"));
    EXPECT_TRUE(fs::exists(fs::path(ROOT) / "Brand New" / "level.dat"));
    EXPECT_TRUE(fs::exists(fs::path(ROOT) / "Brand New" / "chunks" / "c.1.2.dat"));

    // level.dat's worldName field was updated
    std::ifstream f(fs::path(ROOT) / "Brand New" / "level.dat");
    std::string line, found;
    while (std::getline(f, line)) {
        if (line.rfind("worldName=", 0) == 0) found = line;
    }
    EXPECT_EQ(found, "worldName=Brand New");
}

TEST_F(WorldDirectoryTest, RenameToSanitizedName) {
    writeLevelDat(fs::path(ROOT) / "Src", "Src", 1);
    std::string newFolder;
    ASSERT_TRUE(renameWorld("Src", "a/b*c", newFolder, ROOT));
    // Forbidden chars stripped -> "abc"
    EXPECT_EQ(newFolder, "abc");
    EXPECT_TRUE(fs::exists(fs::path(ROOT) / "abc"));
}

TEST_F(WorldDirectoryTest, RenameAppendsCounterOnCollision) {
    writeLevelDat(fs::path(ROOT) / "A", "A", 1);
    writeLevelDat(fs::path(ROOT) / "B", "B", 2);
    std::string newFolder;
    ASSERT_TRUE(renameWorld("A", "B", newFolder, ROOT));
    EXPECT_EQ(newFolder, "B (2)");
    EXPECT_TRUE(fs::exists(fs::path(ROOT) / "B (2)"));
    EXPECT_TRUE(fs::exists(fs::path(ROOT) / "B"));  // untouched
}

// --- relativeTime ---

TEST(WorldDirectoryRelativeTime, Buckets) {
    std::time_t now = 1700000000;  // fixed epoch for determinism
    EXPECT_EQ(relativeTime(now - 5, now), "just now");
    EXPECT_EQ(relativeTime(now - 120, now), "2m ago");
    EXPECT_EQ(relativeTime(now - 7200, now), "2h ago");
    EXPECT_EQ(relativeTime(now - 90000, now), "yesterday");
    EXPECT_EQ(relativeTime(now - 3 * 86400, now), "3 days ago");
}

TEST(WorldDirectoryRelativeTime, NeverForZero) {
    EXPECT_EQ(relativeTime(0), "never");
}
