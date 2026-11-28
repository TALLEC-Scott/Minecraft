#pragma once

// Per-world game mode, saved in level.dat (`gameMode=`). Worlds saved
// before game modes existed have no key and load as Creative, which is how
// they always played.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

enum class GameMode : uint8_t { Survival = 0, Creative = 1 };

inline const char* gameModeName(GameMode m) {
    return m == GameMode::Survival ? "Survival" : "Creative";
}

// /gamemode argument: survival|creative, s|c or 0|1 (any case), as
// Minecraft accepts. nullopt for anything else. Compares in place rather
// than lowercasing a copy: GCC 15 misreports the copy's destruction (inlined
// into the /gamemode lambda) as -Wfree-nonheap-object.
inline std::optional<GameMode> parseGameMode(std::string_view s) {
    auto is = [s](std::string_view word) {
        return std::equal(s.begin(), s.end(), word.begin(), word.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
        });
    };
    if (is("survival") || is("s") || is("0")) return GameMode::Survival;
    if (is("creative") || is("c") || is("1")) return GameMode::Creative;
    return std::nullopt;
}

// Minecraft Java reach, in blocks from the eye: blocks 4.5 survival / 5
// creative, entities 3 / 5.
inline float blockReach(GameMode m) {
    return m == GameMode::Creative ? 5.0f : 4.5f;
}
inline float attackReach(GameMode m) {
    return m == GameMode::Creative ? 5.0f : 3.0f;
}
