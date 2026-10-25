#pragma once

#include "cube.h"
#include "inventory_model.h"
#include "item.h"
#include "ui_renderer.h"
#include "player.h"
#include <GLFW/glfw3.h>
#include <string>

// The E screen. Creative: a palette of every item; clicking one puts it in
// the selected hotbar slot. Survival: the player's 40 slots, moved around
// with a stack carried on the cursor (Minecraft's click rules, see
// clickSlot in inventory_model.h).
class Inventory {
  public:
    bool isOpen() const { return open; }
    void toggle(Player& player) {
        if (open)
            close(player);
        else
            open = true;
    }
    // Puts a stack still carried on the cursor back into the inventory.
    void close(Player& player);

    // Draw the inventory overlay and handle mouse interaction.
    void draw(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Player& player);

    static const char* itemName(ItemId id);

  private:
    bool open = false;
    bool leftWasPressed = false;
    bool rightWasPressed = false;
    int hoveredBlock = -1;
    ItemStack cursor; // stack being carried (Survival)

    void drawCreative(UIRenderer& ui, int windowW, int windowH, double mx, double my, bool clicked, Player& player);
    void drawSurvival(UIRenderer& ui, int windowW, int windowH, double mx, double my, bool leftClick,
                      bool rightClick, bool shift, Player& player);

    // Creative palette.
    static constexpr ItemId ITEMS[] = {
        GRASS,
        DIRT,
        STONE,
        COBBLESTONE,
        COAL_ORE,
        BEDROCK,
        WATER,
        SAND,
        GRAVEL,
        GLOWSTONE,
        WOOD,
        PLANKS,
        LEAVES,
        SNOW,
        CACTUS,
        TNT,
        CRAFTING_TABLE,
        ITEM_STICK,
        ITEM_COAL,
        ITEM_WOODEN_PICKAXE,
        ITEM_WOODEN_AXE,
        ITEM_WOODEN_SHOVEL,
        ITEM_WOODEN_SWORD,
        ITEM_STONE_PICKAXE,
        ITEM_STONE_AXE,
        ITEM_STONE_SHOVEL,
        ITEM_STONE_SWORD,
        ITEM_IRON_SWORD,
        ITEM_APPLE,
        ITEM_RAW_PORKCHOP,
        ITEM_RAW_BEEF,
        ITEM_RAW_CHICKEN,
        ITEM_RAW_MUTTON,
    };
    static constexpr int NUM_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);
    static constexpr int GRID_COLS = 9;
};
