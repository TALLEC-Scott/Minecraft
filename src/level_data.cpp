#include "level_data.h"

#include <algorithm>
#include <istream>
#include <ostream>
#include <sstream>

void writeLevelData(std::ostream& f, const LevelData& data) {
    const PlayerSaveData& player = data.player;
    f << "seed=" << data.seed << "\n";
    if (!data.worldName.empty()) f << "worldName=" << data.worldName << "\n";
    f << "lastPlayed=" << static_cast<long long>(data.lastPlayed) << "\n";
    f << "gameMode=" << static_cast<int>(player.gameMode) << "\n";
    f << "posX=" << player.position.x << "\n";
    f << "posY=" << player.position.y << "\n";
    f << "posZ=" << player.position.z << "\n";
    f << "yaw=" << player.yaw << "\n";
    f << "pitch=" << player.pitch << "\n";
    f << "walkMode=" << (player.walkMode ? 1 : 0) << "\n";
    f << "selectedSlot=" << player.selectedSlot << "\n";
    f << "hotbar=";
    for (int i = 0; i < SAVED_HOTBAR_SIZE; i++) {
        if (i > 0) f << ",";
        f << (int)player.hotbar[i];
    }
    f << "\n";
}

static void readLine(const std::string& key, const std::string& val, LevelData& data) {
    PlayerSaveData& player = data.player;
    if (key == "seed")
        data.seed = (unsigned int)std::stoul(val);
    else if (key == "worldName")
        data.worldName = val;
    else if (key == "lastPlayed")
        data.lastPlayed = static_cast<std::time_t>(std::stoll(val));
    else if (key == "gameMode")
        player.gameMode = std::stoi(val) == 0 ? GameMode::Survival : GameMode::Creative;
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
        player.selectedSlot = std::clamp(std::stoi(val), 0, SAVED_HOTBAR_SIZE - 1);
    else if (key == "hotbar") {
        std::istringstream iss(val);
        std::string token;
        int i = 0;
        while (std::getline(iss, token, ',') && i < SAVED_HOTBAR_SIZE) {
            int id = std::stoi(token);
            bool valid = id >= 0 && id <= 0xFFFF && isValidItem(static_cast<ItemId>(id));
            player.hotbar[i++] = valid ? static_cast<ItemId>(id) : ITEM_EMPTY;
        }
    }
}

void readLevelData(std::istream& in, LevelData& data) {
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        try {
            readLine(line.substr(0, eq), line.substr(eq + 1), data);
        } catch (const std::exception&) {
            // Malformed number: keep the default for this key.
        }
    }
}
