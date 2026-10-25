#pragma once

// level.dat contents: text `key=value` lines with the world seed, its
// metadata and the player's state. Pure (streams only) so it is unit-tested;
// WorldSave does the file I/O. Unknown keys are ignored and a missing or
// malformed value keeps the default, so older and newer files both load.

#include <ctime>
#include <iosfwd>
#include <string>

#include <glm/glm.hpp>

#include "cube.h"
#include "game_mode.h"
#include "inventory_model.h"
#include "item.h"

struct PlayerSaveData {
    glm::vec3 position = glm::vec3(15, 90, 15);
    float yaw = -90.0f;
    float pitch = 0.0f;
    bool walkMode = false;
    // Saved as `inventory=id:count,...`; files from before stacks have only
    // `hotbar=id,...` (one of each).
    PlayerInventory inventory = defaultCreativeInventory();
    int selectedSlot = 0;
    GameMode gameMode = GameMode::Creative; // no key = a pre-game-mode world
};

struct LevelData {
    unsigned int seed = 0;
    std::string worldName; // empty = not written
    std::time_t lastPlayed = 0;
    PlayerSaveData player;
};

void writeLevelData(std::ostream& out, const LevelData& data);
void readLevelData(std::istream& in, LevelData& data);
