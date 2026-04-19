#pragma once

#include "game_state.h"
#include "ui_renderer.h"
#include "widgets.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <string>

class NetSession;

// Background drawer supplied by the caller — usually Menu::drawDirtBackground.
// Using a callable keeps drawMultiplayerMenu free of a Menu dependency.
using BackgroundDrawer = std::function<void(UIRenderer&, int, int)>;

// Panel state for the multiplayer menu. The menu is a standalone module
// rather than extra methods on Menu because it's experimental and self
// contained — the rest of Menu shouldn't have to know about it.
enum class MpPanel { Choose, Host, Join, QuickHost, QuickJoin };

struct MpMenuState {
    MpPanel panel = MpPanel::Choose;
    TextInput offerInput;  // host reads, client writes
    TextInput answerInput; // host writes, client reads
    TextInput codeInput;   // room code for Quick Join
    std::string lastError;
    bool sessionStarted = false;
    // On web, SDP gathering is async. We poll the session for the local
    // description after createOffer/acceptOffer; this flag tells us we're
    // still waiting for ICE gathering to complete.
    bool awaitingLocalSdp = false;
    // True once the host has pasted an answer and we want to transition
    // into Playing the moment the channel opens.
    bool connectingOut = false;

    MpMenuState();
};

// Draw the multiplayer menu. Returns the next GameState — stays as
// GameState::Multiplayer while the user is inside the menu, MainMenu on
// back/cancel, or Playing once the channel is connected.
GameState drawMultiplayerMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Widgets& widgets,
                              MpMenuState& state, NetSession& net, const BackgroundDrawer& drawBg);
