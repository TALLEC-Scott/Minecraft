#include <gtest/gtest.h>
#include <miniaudio.h>

#include <filesystem>
#include <string>

// Every shipped sound must decode through miniaudio: the game loads them
// with ma_sound_init_from_file and silently skips any that fail, so a
// broken decoder (e.g. stb_vorbis not wired into src/miniaudio.cpp) would
// otherwise only show up as a quiet game.
TEST(AudioAssetsTest, EverySoundDecodes) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(PROJECT_ROOT) / "assets" / "Sounds";
    ASSERT_TRUE(fs::is_directory(root)) << root;

    int checked = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".ogg" && ext != ".mp3") continue;

        ma_decoder decoder;
        ASSERT_EQ(ma_decoder_init_file(entry.path().string().c_str(), nullptr, &decoder), MA_SUCCESS) << entry.path();
        ma_uint64 frames = 0;
        EXPECT_EQ(ma_decoder_get_length_in_pcm_frames(&decoder, &frames), MA_SUCCESS) << entry.path();
        EXPECT_GT(frames, 0u) << entry.path();
        ma_decoder_uninit(&decoder);
        checked++;
    }
    EXPECT_GT(checked, 100);
}
