#include "inventory.h"
#include "texture_array.h"

const char* Inventory::itemName(ItemId id) {
    if (id == ITEM_IRON_SWORD) return "Iron Sword";
    switch (itemBlock(id)) {
    case GRASS:
        return "Grass";
    case DIRT:
        return "Dirt";
    case STONE:
        return "Stone";
    case COAL_ORE:
        return "Coal Ore";
    case BEDROCK:
        return "Bedrock";
    case WATER:
        return "Water";
    case SAND:
        return "Sand";
    case GLOWSTONE:
        return "Glowstone";
    case WOOD:
        return "Wood";
    case LEAVES:
        return "Leaves";
    case SNOW:
        return "Snow";
    case GRAVEL:
        return "Gravel";
    case CACTUS:
        return "Cactus";
    case TNT:
        return "TNT";
    default:
        return "?";
    }
}

void Inventory::draw(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Player& player) {
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool clicked = mouseDown && !mouseWasPressed;
    mouseWasPressed = mouseDown;

    constexpr float CELL_SIZE = 48.0f;
    constexpr float CELL_PAD = 4.0f;
    constexpr float ICON_PAD = 6.0f;
    int rows = (NUM_ITEMS + GRID_COLS - 1) / GRID_COLS;
    float gridW = GRID_COLS * (CELL_SIZE + CELL_PAD) - CELL_PAD;
    float gridH = rows * (CELL_SIZE + CELL_PAD) - CELL_PAD;

    // Panel dimensions
    constexpr float PANEL_PAD = 16.0f;
    constexpr float TITLE_H = 30.0f;
    constexpr float NAME_H = 18.0f;
    float panelW = gridW + PANEL_PAD * 2;
    float panelH = TITLE_H + gridH + NAME_H + PANEL_PAD * 2;
    float panelX = (windowW - panelW) / 2.0f;
    float panelY = (windowH - panelH) / 2.0f;

    float gridX = panelX + PANEL_PAD;
    float gridY = panelY + PANEL_PAD + TITLE_H;

    ui.begin(windowW, windowH);

    // Dark overlay
    ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

    // Panel background
    ui.drawRect(panelX, panelY, panelW, panelH, glm::vec4(0.12f, 0.12f, 0.12f, 0.9f));

    // Title
    float titleScale = 2.0f;
    std::string title = "Inventory";
    float titleW = ui.textWidth(title, titleScale);
    ui.drawTextShadow(title, panelX + (panelW - titleW) / 2.0f, panelY + PANEL_PAD, titleScale);

    // Grid
    hoveredBlock = -1;
    for (int i = 0; i < NUM_ITEMS; i++) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;
        float cx = gridX + col * (CELL_SIZE + CELL_PAD);
        float cy = gridY + row * (CELL_SIZE + CELL_PAD);

        bool hovered = (mx >= cx && mx < cx + CELL_SIZE && my >= cy && my < cy + CELL_SIZE);
        if (hovered) hoveredBlock = i;

        // Cell background
        glm::vec4 cellColor = hovered ? glm::vec4(0.5f, 0.5f, 0.5f, 0.6f) : glm::vec4(0.25f, 0.25f, 0.25f, 0.6f);
        ui.drawRect(cx, cy, CELL_SIZE, CELL_SIZE, cellColor);

        // Item icon (top face for blocks)
        GLuint tex = TextureArray::itemIcon(ITEMS[i]);
        if (tex) {
            ui.drawTexturedRect(cx + ICON_PAD, cy + ICON_PAD, CELL_SIZE - ICON_PAD * 2, CELL_SIZE - ICON_PAD * 2, tex,
                                0, 0, 1, 1);
        }

        // Click: assign to selected hotbar slot
        if (hovered && clicked) {
            player.setHotbarSlot(player.getSelectedSlot(), ITEMS[i]);
        }
    }

    // Block name tooltip
    if (hoveredBlock >= 0) {
        const char* name = itemName(ITEMS[hoveredBlock]);
        float nameW = ui.textWidth(name, 1.0f);
        ui.drawTextShadow(name, panelX + (panelW - nameW) / 2.0f, gridY + gridH + CELL_PAD + 2.0f, 1.0f);
    }

    ui.end();
}
