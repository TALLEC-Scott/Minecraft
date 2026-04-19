#include "menu.h"

#include "net/multiplayer_menu.h"
#include "net/net_session.h"

#include <algorithm>
#include <cmath>

void Menu::init() {
    dirtTexture = UIRenderer::loadTexture("assets/Textures/dirt.png", true);

    // Initialize audio engine and preload click sound
    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &audioEngine) == MA_SUCCESS) {
        audioInitialized = true;
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/click.wav", MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                    &clickSound) == MA_SUCCESS) {
            clickLoaded = true;
        }
        // Stream music from disk instead of pre-decoding — MA_SOUND_FLAG_DECODE
        // would eagerly decompress these multi-MB MP3s at startup (~17 MB
        // compressed → ~170 MB PCM), stalling the main menu for seconds.
        // Streaming decodes chunk-by-chunk during playback.
        constexpr ma_uint32 MUSIC_FLAGS = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC;
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/calm1.mp3", MUSIC_FLAGS, nullptr, nullptr,
                                    &musicSound) == MA_SUCCESS) {
            musicLoaded = true;
            ma_sound_set_looping(&musicSound, MA_TRUE);
            ma_sound_set_volume(&musicSound, 1.0f);
        }
        if (ma_sound_init_from_file(&audioEngine, "assets/Sounds/music/menu2.mp3", MUSIC_FLAGS, nullptr, nullptr,
                                    &menuMusicSound) == MA_SUCCESS) {
            menuMusicLoaded = true;
            ma_sound_set_looping(&menuMusicSound, MA_TRUE);
            ma_sound_set_volume(&menuMusicSound, 1.0f);
        }
    }
    // Widgets forwards click audio back to Menu::playClick so sound stays here.
    widgets.setClickSound([this]() { playClick(); });
}

void Menu::destroy() {
    if (dirtTexture) glDeleteTextures(1, &dirtTexture);
    if (menuMusicLoaded) ma_sound_uninit(&menuMusicSound);
    if (musicLoaded) ma_sound_uninit(&musicSound);
    if (clickLoaded) ma_sound_uninit(&clickSound);
    if (audioInitialized) ma_engine_uninit(&audioEngine);
}

void Menu::startMusic() {
    if (musicLoaded && !musicPlaying) {
        stopMenuMusic();
        ma_sound_start(&musicSound);
        musicPlaying = true;
    }
}

void Menu::stopMusic() {
    if (musicLoaded && musicPlaying) {
        ma_sound_stop(&musicSound);
        musicPlaying = false;
    }
}

void Menu::startMenuMusic() {
    if (menuMusicLoaded && !menuMusicPlaying) {
        ma_sound_start(&menuMusicSound);
        menuMusicPlaying = true;
    }
}

void Menu::setMusicVolume(float vol) {
    if (musicLoaded) ma_sound_set_volume(&musicSound, vol);
    if (menuMusicLoaded) ma_sound_set_volume(&menuMusicSound, vol);
}

void Menu::stopMenuMusic() {
    if (menuMusicLoaded && menuMusicPlaying) {
        ma_sound_stop(&menuMusicSound);
        menuMusicPlaying = false;
    }
}

void Menu::playClick() {
    if (clickLoaded) {
        ma_sound_seek_to_pcm_frame(&clickSound, 0);
        ma_sound_start(&clickSound);
    }
}

void Menu::onCharInput(unsigned int codepoint) { widgets.onCharInput(codepoint); }
void Menu::onKeyInput(GLFWwindow* window, int key, int action, int mods) {
    widgets.onKeyInput(window, key, action, mods);
}

void Menu::drawDirtBackground(UIRenderer& ui, int windowW, int windowH) {
    if (!dirtTexture) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.3f, 0.2f, 0.15f, 1.0f));
        return;
    }
    constexpr float TILE_SIZE = 64.0f;
    float tilesX = (float)windowW / TILE_SIZE;
    float tilesY = (float)windowH / TILE_SIZE;
    float scroll = (float)glfwGetTime() * 0.15f;
    ui.drawTexturedRect(0, 0, (float)windowW, (float)windowH, dirtTexture, scroll, scroll, tilesX + scroll,
                        tilesY + scroll, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));
}

GameState Menu::drawMainMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window) {
    widgets.beginFrame(window);
    GameState next = GameState::MainMenu;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    float titleScale = 4.0f;
    std::string title = "MINECRAFT";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy * 0.18f, titleScale,
                      glm::vec4(1.0f, 1.0f, 0.4f, 1.0f));
    float subScale = 1.5f;
    std::string subtitle = "C++ / OpenGL Edition";
    ui.drawTextShadow(subtitle, cx - ui.textWidth(subtitle, subScale) / 2.0f, cy * 0.18f + titleScale * 8.0f + 8.0f,
                      subScale, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    float startY = cy * 0.45f;
    float gap = 56.0f;

    if (widgets.button(ui, "Singleplayer", btnX, startY, btnW, btnH)) next = GameState::WorldList;
    if (widgets.button(ui, "Multiplayer", btnX, startY + gap, btnW, btnH,
                       WidgetOpts{/*enabled=*/true, /*tooltip=*/"Experimental P2P via WebRTC"})) {
        next = GameState::Multiplayer;
    }
    if (widgets.button(ui, "Settings", btnX, startY + gap * 2, btnW, btnH)) {
        settingsReturnState = GameState::MainMenu;
        next = GameState::Settings;
    }
#ifndef __EMSCRIPTEN__
    // Browsers don't let a page programmatically close a tab the user opened,
    // so Quit is desktop-only — GLFW's close flag drives the native main loop.
    if (widgets.button(ui, "Quit Game", btnX, startY + gap * 3, btnW, btnH))
        glfwSetWindowShouldClose(window, true);
#endif

    // Splash text — rotated, pulsing, bottom-right corner
    {
        float t = (float)glfwGetTime();
        float pulse = 2.0f + 0.2f * std::sin(t * 4.0f);
        float px = (float)windowW - 120.0f;
        float py = (float)windowH - 80.0f;
        ui.drawTextRotated("By Sc077y", px, py, pulse, -25.0f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    }
    ui.drawTextShadow("v0.7.12", 4.0f, (float)windowH - 12.0f, 1.0f, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));

    widgets.endFrame(ui);
    ui.end();
    return next;
}

GameState Menu::drawSettings(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, GameSettings& settings) {
    widgets.beginFrame(window);
    GameState next = GameState::Settings;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    float titleScale = 3.0f;
    std::string title = "Settings";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy * 0.08f, titleScale);

    float ctrlW = 400.0f, ctrlH = 40.0f;
    float ctrlX = cx - ctrlW / 2.0f;
    float startY = cy * 0.25f;
    float gap = 52.0f;

    float rd = (float)settings.renderDistance;
    widgets.slider(ui, "Render Distance", 0, ctrlX, startY, ctrlW, ctrlH, rd, 4.0f, 32.0f, " chunks");
    settings.renderDistance = (int)rd;
    widgets.slider(ui, "FOV", 1, ctrlX, startY + gap, ctrlW, ctrlH, settings.fov, 30.0f, 110.0f);
    widgets.slider(ui, "Sensitivity", 2, ctrlX, startY + gap * 2, ctrlW, ctrlH, settings.mouseSensitivity, 0.1f, 3.0f);
    widgets.toggle(ui, "VSync", ctrlX, startY + gap * 3, ctrlW, ctrlH, settings.vsync);
    widgets.toggle(ui, "Greedy Meshing", ctrlX, startY + gap * 4, ctrlW, ctrlH, settings.greedyMeshing);
    widgets.toggle(ui, "Fancy Leaves", ctrlX, startY + gap * 5, ctrlW, ctrlH, settings.fancyLeaves);
    widgets.slider(ui, "Music", 3, ctrlX, startY + gap * 6, ctrlW, ctrlH, settings.musicVolume, 0.0f, 1.0f);

    // Resolution cycler — a button that advances the preset index on click.
    {
        std::string label = std::string("Resolution: ") + RESOLUTION_PRESETS[settings.resolutionIndex].label;
        if (widgets.button(ui, label, ctrlX, startY + gap * 7, ctrlW, ctrlH))
            settings.resolutionIndex = (settings.resolutionIndex + 1) % NUM_RESOLUTION_PRESETS;
    }

    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    if (widgets.button(ui, "Done", btnX, startY + gap * 8.5f, btnW, btnH)) next = settingsReturnState;
    if (widgets.escPressed(window)) next = settingsReturnState;

    widgets.endFrame(ui);
    ui.end();
    return next;
}

GameState Menu::drawMultiplayer(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, NetSession& net) {
    return drawMultiplayerMenu(ui, windowW, windowH, window, widgets, mpState, net);
}

GameState Menu::drawPauseMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window) {
    widgets.beginFrame(window);
    GameState next = GameState::Paused;

    ui.begin(windowW, windowH);
    ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    float titleScale = 3.0f;
    std::string title = "Game Menu";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy * 0.2f, titleScale);

    float btnW = 400.0f, btnH = 44.0f;
    float btnX = cx - btnW / 2.0f;
    float startY = cy * 0.38f;
    float gap = 56.0f;

    if (widgets.button(ui, "Back to Game", btnX, startY, btnW, btnH)) next = GameState::Playing;
    if (widgets.button(ui, "Settings", btnX, startY + gap, btnW, btnH)) {
        settingsReturnState = GameState::Paused;
        next = GameState::Settings;
    }
    if (widgets.button(ui, "Quit to Title", btnX, startY + gap * 2, btnW, btnH)) next = GameState::MainMenu;
    if (widgets.escPressed(window)) next = GameState::Playing;

    widgets.endFrame(ui);
    ui.end();
    return next;
}

// --- World list screen ---

void Menu::refreshWorldList() {
    worlds = listWorlds();
    if (selectedWorld >= (int)worlds.size()) selectedWorld = -1;
    widgets.clearInputLatches();
}

GameState Menu::drawWorldList(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, WorldListResult& out) {
    widgets.beginFrame(window);
    out.action = WorldListResult::None;
    GameState next = GameState::WorldList;

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float titleScale = 3.0f;
    std::string title = "Select World";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, windowH * 0.06f, titleScale);

    float listW = 560.0f, rowH = 44.0f;
    float listX = cx - listW / 2.0f;
    float listY = windowH * 0.18f;
    float listH = windowH * 0.58f;

    ui.drawRect(listX - 2, listY - 2, listW + 4, listH + 4, glm::vec4(0, 0, 0, 0.9f));
    ui.drawRect(listX, listY, listW, listH, glm::vec4(0.08f, 0.08f, 0.10f, 0.85f));

    bool blockInteraction = (overlay != OverlayNone);

    if (worlds.empty()) {
        std::string msg = "No worlds yet. Click 'Create New World' to start.";
        float s = 1.8f;
        ui.drawText(msg, cx - ui.textWidth(msg, s) / 2.0f, listY + listH / 2.0f - ui.textHeight(s) / 2.0f, s,
                    glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
    } else {
        float totalH = worlds.size() * rowH;
        float maxScroll = std::max(0.0f, totalH - listH);
        worldListScroll = std::clamp(worldListScroll, 0.0f, maxScroll);

        for (std::size_t i = 0; i < worlds.size(); ++i) {
            float ry = listY + i * rowH - worldListScroll;
            if (ry + rowH < listY || ry > listY + listH) continue;

            bool selected = ((int)i == selectedWorld);
            double my = widgets.mouseY();
            bool hovered = !blockInteraction && widgets.hoveredRect(listX, ry, listW, rowH) && my >= listY &&
                           my <= listY + listH;
            glm::vec4 bg = selected ? glm::vec4(0.35f, 0.45f, 0.35f, 0.9f)
                                    : hovered ? glm::vec4(0.22f, 0.22f, 0.26f, 0.9f)
                                              : glm::vec4(0.14f, 0.14f, 0.17f, 0.9f);
            ui.drawRect(listX, ry, listW, rowH - 2, bg);

            ui.drawTextShadow(worlds[i].name, listX + 14.0f, ry + 6.0f, 2.0f);
            std::string rel = relativeTime(worlds[i].lastPlayed);
            float relW = ui.textWidth(rel, 1.4f);
            ui.drawTextShadow(rel, listX + listW - relW - 14.0f, ry + rowH - ui.textHeight(1.4f) - 8.0f, 1.4f,
                              glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));

            if (hovered && widgets.clicked()) {
                double now = glfwGetTime();
                bool doublePlay = ((int)i == lastClickedWorld) && (now - lastClickTime) < 0.4;
                selectedWorld = (int)i;
                lastClickedWorld = (int)i;
                lastClickTime = now;
                playClick();
                widgets.consumeClick();
                if (doublePlay) {
                    out.action = WorldListResult::PlaySelected;
                    out.folder = worlds[i].folder;
                    next = GameState::Playing;
                }
            }
        }
    }

    // Footer buttons: two rows
    float btnGap = 12.0f;
    float btnH = 38.0f;
    float footerY = listY + listH + 18.0f;
    float halfW = (listW - btnGap) / 2.0f;
    float thirdW = (listW - 2 * btnGap) / 3.0f;

    bool hasSel = selectedWorld >= 0 && selectedWorld < (int)worlds.size();

    // Footer buttons skip click/hover handling while an overlay is open so
    // the underlying Play/Cancel/etc. can't fire through the dialog.
    auto footerBtn = [&](const char* label, float x, float y, float w, float h, bool enabled = true) {
        return !blockInteraction && widgets.button(ui, label, x, y, w, h, enabled);
    };
    float footerY2 = footerY + btnH + btnGap;
    if (footerBtn("Play Selected", listX, footerY, halfW, btnH, hasSel)) {
        out.action = WorldListResult::PlaySelected;
        out.folder = worlds[selectedWorld].folder;
        next = GameState::Playing;
    }
    if (footerBtn("Create New World", listX + halfW + btnGap, footerY, halfW, btnH)) {
        out.action = WorldListResult::CreateRequested;
        next = GameState::CreateWorld;
    }
    if (footerBtn("Rename", listX, footerY2, thirdW, btnH, hasSel)) {
        overlay = OverlayRename;
        renameInput.active = true;
        renameInput.setText(worlds[selectedWorld].name);
    }
    if (footerBtn("Delete", listX + thirdW + btnGap, footerY2, thirdW, btnH, hasSel)) {
        overlay = OverlayConfirmDelete;
    }
    if (footerBtn("Cancel", listX + 2 * (thirdW + btnGap), footerY2, thirdW, btnH)) {
        out.action = WorldListResult::BackToTitle;
        next = GameState::MainMenu;
    }

    // --- Overlays ---
    if (overlay == OverlayRename) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0, 0, 0, 0.65f));
        float ow = 520.0f, oh = 220.0f;
        float ox = cx - ow / 2.0f, oy = windowH * 0.32f;
        ui.drawRect(ox - 2, oy - 2, ow + 4, oh + 4, glm::vec4(0, 0, 0, 0.95f));
        ui.drawRect(ox, oy, ow, oh, glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
        std::string t = "Rename World";
        ui.drawTextShadow(t, cx - ui.textWidth(t, 2.2f) / 2.0f, oy + 16.0f, 2.2f);

        bool submitted = widgets.textField(ui, renameInput, ox + 20.0f, oy + 72.0f, ow - 40.0f, 40.0f, "New name");
        float bw = (ow - 60.0f) / 2.0f;
        bool confirmClick = widgets.button(ui, "Confirm", ox + 20.0f, oy + oh - 56.0f, bw, 40.0f);
        bool cancelClick = widgets.button(ui, "Cancel", ox + 40.0f + bw, oy + oh - 56.0f, bw, 40.0f);

        if (cancelClick || widgets.escPressedLatch) {
            overlay = OverlayNone;
            renameInput.active = false;
            renameInput.buffer.clear();
        } else if ((confirmClick || submitted) && hasSel) {
            out.action = WorldListResult::RenameConfirmed;
            out.renameOld = worlds[selectedWorld].folder;
            out.renameNew = renameInput.buffer;
            overlay = OverlayNone;
            renameInput.active = false;
        }
    } else if (overlay == OverlayConfirmDelete) {
        ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0, 0, 0, 0.65f));
        float ow = 560.0f, oh = 200.0f;
        float ox = cx - ow / 2.0f, oy = windowH * 0.34f;
        ui.drawRect(ox - 2, oy - 2, ow + 4, oh + 4, glm::vec4(0, 0, 0, 0.95f));
        ui.drawRect(ox, oy, ow, oh, glm::vec4(0.18f, 0.10f, 0.10f, 1.0f));
        std::string t = "Delete World?";
        ui.drawTextShadow(t, cx - ui.textWidth(t, 2.2f) / 2.0f, oy + 16.0f, 2.2f);
        std::string m = hasSel ? ("'" + worlds[selectedWorld].name + "' will be permanently removed.") : "";
        ui.drawText(m, cx - ui.textWidth(m, 1.4f) / 2.0f, oy + 70.0f, 1.4f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        float bw = (ow - 60.0f) / 2.0f;
        bool yes = widgets.button(ui, "Yes, delete", ox + 20.0f, oy + oh - 56.0f, bw, 40.0f);
        bool no = widgets.button(ui, "Cancel", ox + 40.0f + bw, oy + oh - 56.0f, bw, 40.0f);
        if (no || widgets.escPressedLatch) {
            overlay = OverlayNone;
        } else if (yes && hasSel) {
            out.action = WorldListResult::DeleteConfirmed;
            out.folder = worlds[selectedWorld].folder;
            overlay = OverlayNone;
        }
    }

    widgets.endFrame(ui);
    ui.end();
    return next;
}

// --- Create world screen ---

GameState Menu::drawCreateWorld(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, CreateWorldResult& out) {
    widgets.beginFrame(window);
    out.create = false;
    GameState next = GameState::CreateWorld;

    if (!createInitialized) {
        createNameInput.setText(uniqueFolderName(sanitizeWorldName("New World")));
        createNameInput.active = true;
        createSeedInput.setText("");
        createSeedInput.active = false;
        createShowAdvanced = false;
        createInitialized = true;
        widgets.clearInputLatches();
    }

    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float titleScale = 3.0f;
    std::string title = "Create New World";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, windowH * 0.08f, titleScale);

    float panelW = 560.0f;
    float panelX = cx - panelW / 2.0f;
    float y = windowH * 0.22f;

    ui.drawText("World Name", panelX, y, 1.8f);
    y += 28.0f;
    bool nameEnter = widgets.textField(ui, createNameInput, panelX, y, panelW, 40.0f, "My World");
    y += 56.0f;

    std::string adv = createShowAdvanced ? "More World Options (hide)" : "More World Options";
    if (widgets.button(ui, adv, panelX, y, panelW, 36.0f)) createShowAdvanced = !createShowAdvanced;
    y += 52.0f;

    bool seedEnter = false;
    if (createShowAdvanced) {
        ui.drawText("Seed (blank for random; numeric or text)", panelX, y, 1.4f,
                    glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));
        y += 22.0f;
        seedEnter = widgets.textField(ui, createSeedInput, panelX, y, panelW, 40.0f, "blank = random");
        y += 56.0f;
    }

    float footerY = windowH - 80.0f;
    float bw = (panelW - 20.0f) / 2.0f;
    bool createClick = widgets.button(ui, "Create", panelX, footerY, bw, 44.0f);
    bool cancelClick = widgets.button(ui, "Cancel", panelX + bw + 20.0f, footerY, bw, 44.0f);

    // Tab cycles Name <-> Seed (only when advanced visible).
    if (widgets.tabPressedLatch && createShowAdvanced) {
        if (createNameInput.active) {
            createNameInput.active = false;
            createSeedInput.active = true;
            createSeedInput.cursor = createSeedInput.buffer.size();
        } else {
            createSeedInput.active = false;
            createNameInput.active = true;
            createNameInput.cursor = createNameInput.buffer.size();
        }
    }

    if (cancelClick || widgets.escPressedLatch) {
        createInitialized = false;
        createNameInput.active = false;
        createSeedInput.active = false;
        next = GameState::WorldList;
    } else if (createClick || nameEnter || seedEnter) {
        out.create = true;
        out.name = createNameInput.buffer;
        out.seed = createSeedInput.buffer;
        createInitialized = false;
        createNameInput.active = false;
        createSeedInput.active = false;
        next = GameState::Playing;
    }

    widgets.endFrame(ui);
    ui.end();
    return next;
}

// --- Loading screen ---

void Menu::drawLoadingScreen(UIRenderer& ui, int windowW, int windowH, float progress,
                             const std::string& worldName) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    ui.begin(windowW, windowH);
    drawDirtBackground(ui, windowW, windowH);

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH / 2.0f;

    std::string title = "Loading game";
    float titleScale = 3.0f;
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy - 80.0f, titleScale);

    if (!worldName.empty()) {
        float s = 1.8f;
        ui.drawTextShadow(worldName, cx - ui.textWidth(worldName, s) / 2.0f, cy - 30.0f, s,
                          glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
    }

    float barW = 420.0f;
    float barH = 24.0f;
    float barX = cx - barW / 2.0f;
    float barY = cy + 10.0f;
    ui.drawRect(barX - 2, barY - 2, barW + 4, barH + 4, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    ui.drawRect(barX, barY, barW, barH, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f));
    ui.drawRect(barX, barY, barW * progress, barH, glm::vec4(0.35f, 0.65f, 0.35f, 0.95f));

    std::string pct = std::to_string((int)(progress * 100.0f + 0.5f)) + "%";
    float ps = 1.4f;
    ui.drawTextShadow(pct, cx - ui.textWidth(pct, ps) / 2.0f,
                      barY + (barH - ui.textHeight(ps)) / 2.0f, ps);

    ui.end();
}
