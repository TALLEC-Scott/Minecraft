#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "chat_history.h"

namespace {

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

struct TempFile {
    std::string path;
    TempFile() : path((std::filesystem::temp_directory_path() / "mc_chat_history_test.txt").string()) {
        std::remove(path.c_str());
    }
    ~TempFile() { std::remove(path.c_str()); }
};

} // namespace

TEST(ChatHistoryTest, UpWalksBackNewestFirstAndStopsAtOldest) {
    ChatHistory h;
    h.add("/help");
    h.add("/time day");
    h.add("/cowrain");
    std::string s;
    ASSERT_TRUE(h.older("", s));
    EXPECT_EQ(s, "/cowrain");
    ASSERT_TRUE(h.older(s, s));
    EXPECT_EQ(s, "/time day");
    ASSERT_TRUE(h.older(s, s));
    EXPECT_EQ(s, "/help");
    EXPECT_FALSE(h.older(s, s)) << "nothing older than the first entry";
}

TEST(ChatHistoryTest, DownReturnsToTheDraft) {
    ChatHistory h;
    h.add("/help");
    h.add("/time day");
    std::string s;
    ASSERT_TRUE(h.older("/ti", s));
    ASSERT_TRUE(h.older(s, s));
    ASSERT_TRUE(h.newer(s));
    EXPECT_EQ(s, "/time day");
    ASSERT_TRUE(h.newer(s));
    EXPECT_EQ(s, "/ti") << "half-typed line comes back";
    EXPECT_FALSE(h.newer(s));
}

TEST(ChatHistoryTest, EmptyHistoryDoesNothing) {
    ChatHistory h;
    std::string s = "unchanged";
    EXPECT_FALSE(h.older("x", s));
    EXPECT_FALSE(h.newer(s));
    EXPECT_EQ(s, "unchanged");
}

TEST(ChatHistoryTest, IgnoresBlanksAndConsecutiveDuplicates) {
    ChatHistory h;
    h.add("/time day");
    h.add("/time day");
    h.add("");
    h.add("/help");
    h.add("/time day");
    EXPECT_EQ(h.entries(), (std::vector<std::string>{"/time day", "/help", "/time day"}));
}

TEST(ChatHistoryTest, AddingEndsBrowsing) {
    ChatHistory h;
    h.add("/a");
    h.add("/b");
    std::string s;
    ASSERT_TRUE(h.older("", s));
    ASSERT_TRUE(h.older(s, s));
    h.add("/c");
    ASSERT_TRUE(h.older("", s));
    EXPECT_EQ(s, "/c");
}

TEST(ChatHistoryTest, PersistsAcrossLoads) {
    TempFile f;
    {
        ChatHistory h(f.path);
        h.add("/help");
        h.add("/cowrain on");
    }
    ChatHistory h(f.path);
    h.load();
    EXPECT_EQ(h.entries(), (std::vector<std::string>{"/help", "/cowrain on"}));
    EXPECT_EQ(readLines(f.path), h.entries());
}

TEST(ChatHistoryTest, FileIsCappedAtMaxLinesKeepingTheNewest) {
    TempFile f;
    ChatHistory h(f.path);
    const std::size_t total = ChatHistory::MAX_LINES + 25;
    for (std::size_t i = 0; i < total; ++i) h.add("/cmd " + std::to_string(i));
    std::vector<std::string> lines = readLines(f.path);
    ASSERT_EQ(lines.size(), ChatHistory::MAX_LINES);
    EXPECT_EQ(lines.front(), "/cmd 25");
    EXPECT_EQ(lines.back(), "/cmd " + std::to_string(total - 1));
}

TEST(ChatHistoryTest, LoadTrimsAnOversizedFileAndWindowsLineEndings) {
    TempFile f;
    {
        std::ofstream out(f.path);
        for (std::size_t i = 0; i < ChatHistory::MAX_LINES + 5; ++i) out << "/cmd " << i << "\r\n";
    }
    ChatHistory h(f.path);
    h.load();
    ASSERT_EQ(h.entries().size(), ChatHistory::MAX_LINES);
    EXPECT_EQ(h.entries().front(), "/cmd 5");
    EXPECT_EQ(h.entries().back(), "/cmd " + std::to_string(ChatHistory::MAX_LINES + 4));
}
