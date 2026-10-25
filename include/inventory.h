#pragma once

#include "cube.h"
#include "item.h"
#include "ui_renderer.h"
#include "player.h"
#include <GLFW/glfw3.h>
#include <string>

class Inventory {
  public:
    bool isOpen() const { return open; }
    void toggle() { open = !open; }
    void close() { open = false; }

    // Draw the inventory overlay and handle mouse interaction.
    void draw(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Player& player);

  private:
    bool open = false;
    bool mouseWasPressed = false;
    int hoveredBlock = -1;

    static constexpr ItemId ITEMS[] = {
        GRASS,  DIRT, STONE,  COAL_ORE, BEDROCK, WATER,           SAND, GLOWSTONE, WOOD,
        LEAVES, SNOW, GRAVEL, CACTUS,   TNT,     ITEM_IRON_SWORD,
    };
    static constexpr int NUM_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);
    static constexpr int GRID_COLS = 9;

    static const char* itemName(ItemId id);
};
