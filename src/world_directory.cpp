#include "world_directory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxName = 48;
const std::string kForbidden = "/\\:*?\"<>|";

std::time_t mtimeOf(const fs::path& p) {
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    // Portable conversion that works on libstdc++ / libc++ / Emscripten.
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

// Lightweight level.dat reader used only by listWorlds() — avoids pulling
// in the full WorldSave (which depends on chunk/section types).
void readLevelMeta(const fs::path& levelDat, std::string& nameOut, std::time_t& lastPlayedOut) {
    std::ifstream f(levelDat);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (k == "worldName") nameOut = v;
        else if (k == "lastPlayed") {
            try { lastPlayedOut = static_cast<std::time_t>(std::stoll(v)); } catch (...) {}
        }
    }
}

}  // namespace

std::string sanitizeWorldName(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (kForbidden.find(c) != std::string::npos) continue;
        if (static_cast<unsigned char>(c) < 0x20) continue;  // strip control chars
        out.push_back(c);
    }
    // Trim whitespace
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), notSpace));
    out.erase(std::find_if(out.rbegin(), out.rend(), notSpace).base(), out.end());
    if (out.size() > kMaxName) out.resize(kMaxName);
    if (out.empty()) out = "World";
    return out;
}

std::string uniqueFolderName(const std::string& base, const std::string& savesRoot) {
    fs::path root(savesRoot);
    if (!fs::exists(root / base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::string candidate = base + " (" + std::to_string(i) + ")";
        if (!fs::exists(root / candidate)) return candidate;
    }
    return base + " (999)";
}

std::uint32_t resolveSeed(const std::string& raw) {
    if (raw.empty()) {
        std::random_device rd;
        return rd();
    }
    bool allDigits = std::all_of(raw.begin(), raw.end(),
                                 [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    if (allDigits) {
        try {
            return static_cast<std::uint32_t>(std::stoull(raw));
        } catch (...) {
            // Overflow -> fall through to hash
        }
    }
    std::uint32_t h = 2166136261u;  // FNV-1a 32-bit
    for (unsigned char c : raw) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::vector<WorldEntry> listWorlds(const std::string& savesRoot) {
    std::vector<WorldEntry> out;
    fs::path root(savesRoot);
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return out;
    for (auto& entry : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        auto levelDat = entry.path() / "level.dat";
        if (!fs::exists(levelDat)) continue;
        WorldEntry w;
        w.folder = entry.path().filename().string();
        w.name.clear();
        w.lastPlayed = 0;
        readLevelMeta(levelDat, w.name, w.lastPlayed);
        if (w.name.empty()) w.name = w.folder;
        if (w.lastPlayed == 0) w.lastPlayed = mtimeOf(levelDat);
        out.push_back(std::move(w));
    }
    std::sort(out.begin(), out.end(),
              [](const WorldEntry& a, const WorldEntry& b) { return a.lastPlayed > b.lastPlayed; });
    return out;
}

bool deleteWorld(const std::string& folder, const std::string& savesRoot) {
    fs::path p = fs::path(savesRoot) / folder;
    std::error_code ec;
    fs::remove_all(p, ec);
    return !ec;
}

bool renameWorld(const std::string& oldFolder, const std::string& newDisplayName, std::string& newFolderOut,
                 const std::string& savesRoot) {
    fs::path root(savesRoot);
    fs::path oldPath = root / oldFolder;
    if (!fs::exists(oldPath)) return false;
    std::string sanitized = sanitizeWorldName(newDisplayName);
    std::string target = (sanitized == oldFolder) ? oldFolder : uniqueFolderName(sanitized, savesRoot);
    fs::path newPath = root / target;
    if (target != oldFolder) {
        std::error_code ec;
        fs::rename(oldPath, newPath, ec);
        if (ec) return false;
    }
    newFolderOut = target;
    // Rewrite level.dat with updated worldName (preserve all other keys).
    fs::path levelDat = newPath / "level.dat";
    std::ifstream in(levelDat);
    if (!in.is_open()) return false;
    std::stringstream rewritten;
    std::string line;
    bool sawName = false;
    while (std::getline(in, line)) {
        if (line.rfind("worldName=", 0) == 0) {
            rewritten << "worldName=" << sanitized << "\n";
            sawName = true;
        } else {
            rewritten << line << "\n";
        }
    }
    in.close();
    if (!sawName) rewritten << "worldName=" << sanitized << "\n";
    std::ofstream out(levelDat, std::ios::trunc);
    if (!out.is_open()) return false;
    out << rewritten.str();
    return true;
}

std::string relativeTime(std::time_t then, std::time_t now) {
    if (then <= 0) return "never";
    if (now == 0) now = std::time(nullptr);
    long long diff = static_cast<long long>(now - then);
    if (diff < 0) diff = 0;
    if (diff < 60) return "just now";
    if (diff < 3600) {
        long long m = diff / 60;
        return std::to_string(m) + "m ago";
    }
    if (diff < 86400) {
        long long h = diff / 3600;
        return std::to_string(h) + "h ago";
    }
    if (diff < 2 * 86400) return "yesterday";
    if (diff < 14 * 86400) {
        long long d = diff / 86400;
        return std::to_string(d) + " days ago";
    }
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &then);
#else
    localtime_r(&then, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}
