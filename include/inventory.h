#pragma once

#include "cube.h"
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

    static constexpr block_type PLACEABLE_BLOCKS[] = {
        GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE, WOOD, LEAVES, SNOW, GRAVEL, CACTUS, TNT,
    };
    static constexpr int NUM_BLOCKS = sizeof(PLACEABLE_BLOCKS) / sizeof(PLACEABLE_BLOCKS[0]);
    static constexpr int GRID_COLS = 9;

    static const char* blockName(block_type t);
};
