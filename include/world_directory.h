#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct WorldEntry {
    std::string folder;      // path segment under saves/
    std::string name;        // display name (from level.dat, else folder)
    std::time_t lastPlayed;  // from level.dat, else filesystem mtime
};

// Enumerates saves/*/level.dat, sorted by lastPlayed descending.
std::vector<WorldEntry> listWorlds(const std::string& savesRoot = "saves");

// Recursively removes saves/<folder>.
bool deleteWorld(const std::string& folder, const std::string& savesRoot = "saves");

// Renames saves/<oldFolder> to a new folder derived from newDisplayName.
// Updates level.dat's worldName. Returns the chosen new folder via out param.
bool renameWorld(const std::string& oldFolder, const std::string& newDisplayName, std::string& newFolderOut,
                 const std::string& savesRoot = "saves");

// Strip forbidden chars (/\:*?"<>|), trim, clamp to 48 chars. Empty fallback to "World".
std::string sanitizeWorldName(const std::string& raw);

// Returns `base`, or `base (2)` / `base (3)` / ... if `saves/<base>` already exists.
std::string uniqueFolderName(const std::string& base, const std::string& savesRoot = "saves");

// Blank -> random. All-digits -> parsed (overflow falls through). Else FNV-1a 32-bit hash.
std::uint32_t resolveSeed(const std::string& raw);

// "just now" / "3m ago" / "2h ago" / "yesterday" / "3 days ago" / "YYYY-MM-DD".
std::string relativeTime(std::time_t then, std::time_t now = 0);
