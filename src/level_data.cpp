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
    f << "health=" << player.health << "\n";
    f << "food=" << player.food << "\n";
    f << "saturation=" << player.saturation << "\n";
    f << "inventory=";
    for (int i = 0; i < PlayerInventory::SIZE; i++) {
        const ItemStack& stack = player.inventory.slots[i];
        if (i > 0) f << ",";
        f << (stack.empty() ? 0 : (int)stack.id) << ":" << (stack.empty() ? 0 : stack.count);
    }
    f << "\n";
}

static bool validId(int id) {
    return id > 0 && id <= 0xFFFF && isValidItem(static_cast<ItemId>(id));
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
    else if (key == "health")
        player.health = std::clamp(std::stof(val), 1.0f, 20.0f);
    else if (key == "food")
        player.food = std::clamp(std::stoi(val), 0, 20);
    else if (key == "saturation")
        player.saturation = std::clamp(std::stof(val), 0.0f, 20.0f);
    else if (key == "selectedSlot")
        player.selectedSlot = std::clamp(std::stoi(val), 0, PlayerInventory::HOTBAR_SIZE - 1);
    else if (key == "hotbar") { // pre-stack files: one of each
        std::istringstream iss(val);
        std::string token;
        int i = 0;
        while (std::getline(iss, token, ',') && i < PlayerInventory::HOTBAR_SIZE) {
            int id = std::stoi(token);
            ItemStack& slot = player.inventory.slots[i++];
            if (validId(id))
                slot = {static_cast<ItemId>(id), 1};
            else
                slot.clear();
        }
    } else if (key == "inventory") {
        player.inventory.clear();
        std::istringstream iss(val);
        std::string token;
        int i = 0;
        while (std::getline(iss, token, ',') && i < PlayerInventory::SIZE) {
            ItemStack& slot = player.inventory.slots[i++];
            auto colon = token.find(':');
            if (colon == std::string::npos) continue;
            try { // one bad entry only empties its own slot
                int id = std::stoi(token.substr(0, colon));
                int count = std::stoi(token.substr(colon + 1));
                if (validId(id) && count > 0)
                    slot = {static_cast<ItemId>(id), std::min(count, maxStackSize(static_cast<ItemId>(id)))};
            } catch (const std::exception&) {
            }
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
