#include "inventory.h"
#include "texture_array.h"

const char* Inventory::itemName(ItemId id) {
    if (const ItemInfo* info = itemInfo(id)) return info->name;
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
    case TALL_GRASS:
        return "Grass";
    case DANDELION:
        return "Dandelion";
    case POPPY:
        return "Poppy";
    case PLANKS:
        return "Oak Planks";
    case COBBLESTONE:
        return "Cobblestone";
    case CRAFTING_TABLE:
        return "Crafting Table";
    default:
        return "?";
    }
}

// Item icon + stack count (when more than one), as on the hotbar.
static void drawStack(UIRenderer& ui, const ItemStack& stack, float x, float y, float size) {
    if (stack.empty()) return;
    constexpr float ICON_PAD = 6.0f;
    GLuint tex = TextureArray::itemIcon(stack.id);
    if (tex) ui.drawTexturedRect(x + ICON_PAD, y + ICON_PAD, size - ICON_PAD * 2, size - ICON_PAD * 2, tex, 0, 0, 1, 1);
    if (stack.count > 1) {
        std::string count = std::to_string(stack.count);
        ui.drawTextShadow(count, x + size - 3 - ui.textWidth(count, 1.5f), y + size - 15, 1.5f);
    }
}

void Inventory::close(Player& player) {
    open = false;
    // Whatever doesn't fit back is lost for now (no item drops yet).
    if (!cursor.empty()) player.getInventory().add(cursor.id, cursor.count);
    cursor.clear();
}

void Inventory::draw(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Player& player) {
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool leftClick = leftDown && !leftWasPressed;
    bool rightClick = rightDown && !rightWasPressed;
    leftWasPressed = leftDown;
    rightWasPressed = rightDown;
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    ui.begin(windowW, windowH);
    // Dark overlay
    ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
    if (player.isSurvival())
        drawSurvival(ui, windowW, windowH, mx, my, leftClick, rightClick, shift, player);
    else
        drawCreative(ui, windowW, windowH, mx, my, leftClick, player);
    ui.end();
}

void Inventory::drawCreative(UIRenderer& ui, int windowW, int windowH, double mx, double my, bool clicked,
                             Player& player) {
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
            player.getInventory().slots[player.getSelectedSlot()] = {ITEMS[i], 1};
        }
    }

    // Block name tooltip
    if (hoveredBlock >= 0) {
        const char* name = itemName(ITEMS[hoveredBlock]);
        float nameW = ui.textWidth(name, 1.0f);
        ui.drawTextShadow(name, panelX + (panelW - nameW) / 2.0f, gridY + gridH + CELL_PAD + 2.0f, 1.0f);
    }
}

void Inventory::drawSurvival(UIRenderer& ui, int windowW, int windowH, double mx, double my, bool leftClick,
                             bool rightClick, bool shift, Player& player) {
    constexpr float CELL_SIZE = 48.0f;
    constexpr float CELL_PAD = 4.0f;
    constexpr float PANEL_PAD = 16.0f;
    constexpr float TITLE_H = 30.0f;
    constexpr float HOTBAR_GAP = 12.0f;
    constexpr float NAME_H = 18.0f;
    constexpr int COLS = PlayerInventory::HOTBAR_SIZE;
    constexpr int STORAGE_ROWS = PlayerInventory::STORAGE_SIZE / COLS;

    float gridW = COLS * (CELL_SIZE + CELL_PAD) - CELL_PAD;
    float storageH = STORAGE_ROWS * (CELL_SIZE + CELL_PAD) - CELL_PAD;
    float panelW = gridW + PANEL_PAD * 2;
    float panelH = TITLE_H + storageH + HOTBAR_GAP + CELL_SIZE + NAME_H + PANEL_PAD * 2;
    float panelX = (windowW - panelW) / 2.0f;
    float panelY = (windowH - panelH) / 2.0f;
    float gridX = panelX + PANEL_PAD;
    float storageY = panelY + PANEL_PAD + TITLE_H;
    float hotbarY = storageY + storageH + HOTBAR_GAP;

    ui.drawRect(panelX, panelY, panelW, panelH, glm::vec4(0.12f, 0.12f, 0.12f, 0.9f));
    float titleScale = 2.0f;
    std::string title = "Inventory";
    ui.drawTextShadow(title, panelX + (panelW - ui.textWidth(title, titleScale)) / 2.0f, panelY + PANEL_PAD,
                      titleScale);

    PlayerInventory& inv = player.getInventory();
    int hovered = -1;
    // Storage rows on top, the hotbar row below (as in Minecraft).
    for (int i = 0; i < PlayerInventory::SIZE; i++) {
        bool hotbar = i < PlayerInventory::HOTBAR_SIZE;
        int col = hotbar ? i : (i - PlayerInventory::HOTBAR_SIZE) % COLS;
        int row = hotbar ? 0 : (i - PlayerInventory::HOTBAR_SIZE) / COLS;
        float cx = gridX + col * (CELL_SIZE + CELL_PAD);
        float cy = hotbar ? hotbarY : storageY + row * (CELL_SIZE + CELL_PAD);
        bool over = mx >= cx && mx < cx + CELL_SIZE && my >= cy && my < cy + CELL_SIZE;
        if (over) hovered = i;
        ui.drawRect(cx, cy, CELL_SIZE, CELL_SIZE,
                    over ? glm::vec4(0.5f, 0.5f, 0.5f, 0.6f) : glm::vec4(0.25f, 0.25f, 0.25f, 0.6f));
        drawStack(ui, inv.slots[i], cx, cy, CELL_SIZE);
    }

    if (hovered >= 0) {
        if (leftClick && shift && cursor.empty())
            inv.quickMove(hovered);
        else if (leftClick || rightClick)
            clickSlot(inv.slots[hovered], cursor, rightClick);
    }

    // Name of the hovered item, under the grid.
    if (hovered >= 0 && cursor.empty() && !inv.slots[hovered].empty()) {
        const char* name = itemName(inv.slots[hovered].id);
        ui.drawTextShadow(name, panelX + (panelW - ui.textWidth(name, 1.0f)) / 2.0f, hotbarY + CELL_SIZE + 6.0f,
                          1.0f);
    }

    // The carried stack follows the mouse.
    drawStack(ui, cursor, (float)mx - CELL_SIZE / 2.0f, (float)my - CELL_SIZE / 2.0f, CELL_SIZE);
}
