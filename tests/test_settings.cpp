#include <gtest/gtest.h>
#include "game_state.h"
#include <fstream>
#include <cstdio>

class SettingsTest : public ::testing::Test {
  protected:
    std::string tmpFile = "test_settings_tmp.txt";

    void TearDown() override { std::remove(tmpFile.c_str()); }

    void writeFile(const std::string& content) {
        std::ofstream f(tmpFile);
        f << content;
    }
};

TEST_F(SettingsTest, DefaultValues) {
    GameSettings s;
    EXPECT_EQ(s.renderDistance, 16);
    EXPECT_FLOAT_EQ(s.fov, 70.0f);
    EXPECT_FALSE(s.vsync);
    EXPECT_FLOAT_EQ(s.mouseSensitivity, 1.0f);
    EXPECT_FALSE(s.greedyMeshing);
}

TEST_F(SettingsTest, LoadFromFile) {
    writeFile("renderDistance=12\nfov=90\nvsync=1\nmouseSensitivity=2.5\ngreedyMeshing=1\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_EQ(s.renderDistance, 12);
    EXPECT_FLOAT_EQ(s.fov, 90.0f);
    EXPECT_TRUE(s.vsync);
    EXPECT_FLOAT_EQ(s.mouseSensitivity, 2.5f);
    EXPECT_TRUE(s.greedyMeshing);
}

TEST_F(SettingsTest, ClampRenderDistance) {
    writeFile("renderDistance=100\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_EQ(s.renderDistance, 32);

    writeFile("renderDistance=1\n");
    s = GameSettings{};
    s.load(tmpFile);
    EXPECT_EQ(s.renderDistance, 4);
}

TEST_F(SettingsTest, ClampFOV) {
    writeFile("fov=200\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.fov, 110.0f);

    writeFile("fov=5\n");
    s = GameSettings{};
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.fov, 30.0f);
}

TEST_F(SettingsTest, ClampSensitivity) {
    writeFile("mouseSensitivity=10\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.mouseSensitivity, 3.0f);

    writeFile("mouseSensitivity=0.01\n");
    s = GameSettings{};
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.mouseSensitivity, 0.1f);
}

TEST_F(SettingsTest, SaveAndReload) {
    GameSettings s;
    s.renderDistance = 10;
    s.fov = 85.0f;
    s.vsync = true;
    s.mouseSensitivity = 1.5f;
    s.greedyMeshing = true;
    s.save(tmpFile);

    GameSettings loaded;
    loaded.load(tmpFile);
    EXPECT_EQ(loaded.renderDistance, 10);
    EXPECT_FLOAT_EQ(loaded.fov, 85.0f);
    EXPECT_TRUE(loaded.vsync);
    EXPECT_FLOAT_EQ(loaded.mouseSensitivity, 1.5f);
    EXPECT_TRUE(loaded.greedyMeshing);
}

TEST_F(SettingsTest, MissingFileKeepsDefaults) {
    GameSettings s;
    s.load("nonexistent_file_xyz.txt");
    EXPECT_EQ(s.renderDistance, 16);
    EXPECT_FLOAT_EQ(s.fov, 70.0f);
    EXPECT_FALSE(s.vsync);
}

TEST_F(SettingsTest, PartialFileKeepsOtherDefaults) {
    writeFile("fov=100\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.fov, 100.0f);
    EXPECT_EQ(s.renderDistance, 16); // unchanged
    EXPECT_FALSE(s.vsync);          // unchanged
}

TEST_F(SettingsTest, UnknownKeysIgnored) {
    writeFile("fov=80\nunknownKey=999\nrenderDistance=20\n");
    GameSettings s;
    s.load(tmpFile);
    EXPECT_FLOAT_EQ(s.fov, 80.0f);
    EXPECT_EQ(s.renderDistance, 20);
}

TEST(GameStateTest, EnumValues) {
    // Verify all states exist and are distinct
    EXPECT_NE(GameState::MainMenu, GameState::Playing);
    EXPECT_NE(GameState::Playing, GameState::Paused);
    EXPECT_NE(GameState::Paused, GameState::Settings);
    EXPECT_NE(GameState::Settings, GameState::MainMenu);
}
