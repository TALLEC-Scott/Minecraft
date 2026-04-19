#include "net/multiplayer_menu.h"

#include "net/net_session.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

// SDP JSON strings are ~1.5 KB. Bump the text-input buffer cap just for
// these two fields; other inputs keep their 48-char default.
constexpr std::size_t SDP_MAX_LEN = 8192;

void setLarge(TextInput& in) {
    in.maxLen = SDP_MAX_LEN;
}

// Copy a string into the browser clipboard (web only). On secure contexts
// we use the async Clipboard API; otherwise (plain HTTP, where that API
// is absent) we fall back to a native prompt() dialog pre-filled with the
// text so the user can select + Ctrl+C manually. No-op on desktop.
void copyToClipboard(const std::string& s) {
#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_ASM({
        var text = UTF8ToString($0);
        function manualCopy() { window.prompt("Copy the SDP (Ctrl+C, then Enter):", text); }
        if (navigator.clipboard && navigator.clipboard.writeText && window.isSecureContext) {
            navigator.clipboard.writeText(text).catch(manualCopy);
        } else {
            manualCopy();
        }
    }, s.c_str());
    // clang-format on
#else
    (void)s;
#endif
}

// Fetch current clipboard text — the textField widget can't accept paste
// characters >0x7E, and some users' SDP base64 includes newlines, so we
// offer a "Paste from clipboard" button that writes into the input buffer
// directly.
#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(void, mpmenu_request_clipboard, (int slot), {
    function save(text) {
        if (text == null || text === "") return;
        if (!Module._mpClipboard) Module._mpClipboard = {};
        Module._mpClipboard[slot] = text;
    }
    // Secure context: async Clipboard API (no dialog). Otherwise (plain
    // HTTP, where readText is undefined) fall back to prompt() so the
    // user can paste into a native dialog with Ctrl+V.
    if (navigator.clipboard && navigator.clipboard.readText && window.isSecureContext) {
        navigator.clipboard.readText()
            .then(save)
            .catch(function() { save(window.prompt("Paste the SDP here (Ctrl+V, Enter):", "")); });
    } else {
        save(window.prompt("Paste the SDP here (Ctrl+V, Enter):", ""));
    }
});
EM_JS(const char*, mpmenu_take_clipboard, (int slot), {
    if (!Module._mpClipboard || !Module._mpClipboard[slot]) return 0;
    var text = Module._mpClipboard[slot];
    Module._mpClipboard[slot] = null;
    var bytes = lengthBytesUTF8(text) + 1;
    var ptr = _malloc(bytes);
    stringToUTF8(text, ptr, bytes);
    return ptr;
});
// clang-format on
#endif

void requestClipboardInto(TextInput& in, int slot) {
#ifdef __EMSCRIPTEN__
    mpmenu_request_clipboard(slot);
    // The async read will complete within a frame or two; pollClipboard()
    // (below) picks it up from the next drawMultiplayerMenu call.
    (void)in;
#else
    (void)in;
    (void)slot;
#endif
}

void pollClipboard(TextInput& in, int slot) {
#ifdef __EMSCRIPTEN__
    const char* p = mpmenu_take_clipboard(slot);
    if (!p) return;
    std::string text(p);
    std::free((void*)p);
    if (text.size() > in.maxLen) text.resize(in.maxLen);
    in.buffer = text;
    in.cursor = in.buffer.size();
#else
    (void)in;
    (void)slot;
#endif
}

// Word-wrap `text` to fit `maxWidth` pixels at the given scale. Breaks on
// spaces; if a single word is wider than `maxWidth` it gets its own line
// (character-level breaking would look worse for sentence copy). Returns
// the rendered height so callers can reserve vertical space.
float drawWrappedText(UIRenderer& ui, const std::string& text, float x, float y, float maxWidth, float scale,
                      glm::vec4 color) {
    float glyphW = UIRenderer::GLYPH_W * scale;
    int maxChars = (glyphW > 0.0f) ? static_cast<int>(maxWidth / glyphW) : 1;
    if (maxChars < 1) maxChars = 1;
    float rowH = ui.textHeight(scale) + 4.0f;

    // Split on spaces, then greedily pack words into lines.
    std::string line;
    float curY = y;
    auto flush = [&]() {
        if (!line.empty()) {
            ui.drawText(line, x, curY, scale, color);
            curY += rowH;
            line.clear();
        }
    };
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(i, end - i);
        i = (end < text.size()) ? end + 1 : end;
        if (word.empty()) continue;
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1 + word.size() <= static_cast<std::size_t>(maxChars)) {
            line += ' ';
            line += word;
        } else {
            flush();
            line = word;
        }
    }
    flush();
    return curY - y;
}

// Render a scrollable read-only "text area" showing the given string. We
// don't have a true multi-line widget in widgets.cpp, so we clip a
// bordered box and draw up to `lines` wrapped rows.
void drawReadonlyArea(UIRenderer& ui, float x, float y, float w, float h, const std::string& text) {
    ui.drawBorderedRect(x, y, w, h, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f), glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    if (text.empty()) {
        ui.drawText("(waiting for local SDP...)", x + 8, y + 8, 1.2f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        return;
    }
    constexpr float scale = 1.1f;
    constexpr float pad = 8.0f;
    float rowH = ui.textHeight(scale) + 2.0f;
    float charsPerLine = (w - pad * 2) / (UIRenderer::GLYPH_W * scale);
    int maxChars = (charsPerLine > 1) ? static_cast<int>(charsPerLine) : 1;
    int maxRows = static_cast<int>((h - pad * 2) / rowH);
    if (maxRows < 1) maxRows = 1;
    int drawn = 0;
    for (std::size_t i = 0; i < text.size() && drawn < maxRows;) {
        std::size_t n = std::min<std::size_t>(maxChars, text.size() - i);
        // Filter to printable ASCII so the bitmap font doesn't emit garbage.
        std::string line;
        line.reserve(n);
        for (std::size_t k = 0; k < n; ++k) {
            char c = text[i + k];
            line += (c >= 0x20 && c <= 0x7E) ? c : '?';
        }
        ui.drawText(line, x + pad, y + pad + drawn * rowH, scale);
        i += n;
        ++drawn;
    }
    if (text.size() > static_cast<std::size_t>(maxChars * maxRows)) {
        std::string more = "(...truncated, use Copy)";
        ui.drawText(more, x + pad, y + h - rowH - 2.0f, scale, glm::vec4(0.7f, 0.7f, 0.4f, 1.0f));
    }
}

} // namespace

MpMenuState::MpMenuState() {
    setLarge(offerInput);
    setLarge(answerInput);
}

GameState drawMultiplayerMenu(UIRenderer& ui, int windowW, int windowH, GLFWwindow* window, Widgets& widgets,
                              MpMenuState& state, NetSession& net) {
    widgets.beginFrame(window);
    GameState next = GameState::Multiplayer;

    ui.begin(windowW, windowH);
    ui.drawRect(0, 0, (float)windowW, (float)windowH, glm::vec4(0.15f, 0.12f, 0.09f, 1.0f));

    float cx = (float)windowW / 2.0f;
    float cy = (float)windowH;

    float titleScale = 3.0f;
    std::string title = "Multiplayer (experimental)";
    ui.drawTextShadow(title, cx - ui.textWidth(title, titleScale) / 2.0f, cy * 0.07f, titleScale);

    // Availability banner.
    if (!net.available()) {
        std::string msg = "WebRTC is only available in the web build.";
        float s = 1.6f;
        ui.drawText(msg, cx - ui.textWidth(msg, s) / 2.0f, cy * 0.18f, s, glm::vec4(1.0f, 0.7f, 0.4f, 1.0f));
        float btnW = 400.0f, btnH = 44.0f;
        float btnX = cx - btnW / 2.0f;
        if (widgets.button(ui, "Back", btnX, cy * 0.8f, btnW, btnH)) {
            next = GameState::MainMenu;
            state = MpMenuState{};
        }
        if (widgets.escPressed(window)) next = GameState::MainMenu;
        widgets.endFrame(ui);
        ui.end();
        return next;
    }

    // Poll clipboard requests: if a previous frame asked for the clipboard,
    // the async browser read may have completed now.
    pollClipboard(state.offerInput, 0);
    pollClipboard(state.answerInput, 1);

    // If we're connected, drop straight into Playing.
    if (net.connected() && state.connectingOut) {
        state.connectingOut = false;
        widgets.endFrame(ui);
        ui.end();
        return GameState::Playing;
    }

    // If we created a session but the SDP isn't ready yet, keep polling.
    // readLocalSdp is non-destructive — it just asks the JS side for the
    // current localDescription, which ICE gathering fills in asynchronously.
    if (state.awaitingLocalSdp) {
        std::string sdp = net.readLocalSdp();
        if (!sdp.empty()) {
            if (state.panel == MpPanel::Host) {
                state.offerInput.buffer = sdp;
                state.offerInput.cursor = sdp.size();
            } else if (state.panel == MpPanel::Join) {
                state.answerInput.buffer = sdp;
                state.answerInput.cursor = sdp.size();
            }
            state.awaitingLocalSdp = false;
        }
    }

    float panelTopY = cy * 0.2f;
    float btnW = 220.0f, btnH = 44.0f;

    if (state.panel == MpPanel::Choose) {
        float gap = 40.0f;
        float row1X = cx - btnW - gap / 2.0f;
        float row2X = cx + gap / 2.0f;
        if (widgets.button(ui, "Host a game", row1X, panelTopY + 40.0f, btnW, btnH)) {
            state.panel = MpPanel::Host;
            std::string sdp = net.createOffer();
            state.offerInput.buffer = sdp;
            state.offerInput.cursor = sdp.size();
            state.awaitingLocalSdp = sdp.empty();
            state.sessionStarted = true;
        }
        if (widgets.button(ui, "Join a game", row2X, panelTopY + 40.0f, btnW, btnH)) {
            state.panel = MpPanel::Join;
            state.sessionStarted = false;
        }
        std::string hint = "Host generates an SDP offer; share it with your peer. Peer pastes it on the Join side and "
                           "returns an answer SDP. Host pastes the answer and clicks Connect.";
        float s = 1.2f;
        // Cap the wrap width so the hint never runs past the viewport edge
        // on narrower windows (the old drawText call ignored newlines and
        // spilled off-screen).
        float hintWidth = std::min(static_cast<float>(windowW) - 40.0f, 640.0f);
        drawWrappedText(ui, hint, cx - hintWidth / 2.0f, panelTopY + 130.0f, hintWidth, s,
                        glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));

        float backX = cx - btnW / 2.0f;
        if (widgets.button(ui, "Back", backX, cy * 0.85f, btnW, btnH)) {
            next = GameState::MainMenu;
            state = MpMenuState{};
        }
        if (widgets.escPressed(window)) {
            next = GameState::MainMenu;
            state = MpMenuState{};
        }
    } else if (state.panel == MpPanel::Host) {
        float labelY = panelTopY;
        ui.drawText("Offer SDP (copy this to your peer):", cx - 330.0f, labelY, 1.4f);
        drawReadonlyArea(ui, cx - 330.0f, labelY + 22.0f, 660.0f, 110.0f, state.offerInput.buffer);

        float row1Y = labelY + 140.0f;
        if (widgets.button(ui, "Copy offer", cx - btnW - 10.0f, row1Y, btnW, 36.0f)) {
            if (!state.offerInput.buffer.empty()) copyToClipboard(state.offerInput.buffer);
        }
        if (widgets.button(ui, "Refresh", cx + 10.0f, row1Y, btnW, 36.0f)) {
            std::string sdp = net.readLocalSdp();
            if (!sdp.empty()) {
                state.offerInput.buffer = sdp;
                state.offerInput.cursor = sdp.size();
            }
        }

        float ansY = row1Y + 50.0f;
        ui.drawText("Paste peer's answer below:", cx - 330.0f, ansY, 1.4f);
        widgets.textField(ui, state.answerInput, cx - 330.0f, ansY + 22.0f, 500.0f, 36.0f, "paste answer JSON");
        if (widgets.button(ui, "Paste", cx + 180.0f, ansY + 22.0f, 150.0f, 36.0f)) {
            requestClipboardInto(state.answerInput, 1);
        }

        float footerY = ansY + 80.0f;
        if (widgets.button(ui, "Connect", cx - btnW - 10.0f, footerY, btnW, btnH)) {
            if (!state.answerInput.buffer.empty()) {
                net.acceptAnswer(state.answerInput.buffer);
                state.connectingOut = true;
            } else {
                state.lastError = "Answer SDP is empty.";
            }
        }
        if (widgets.button(ui, "Back", cx + 10.0f, footerY, btnW, btnH)) {
            net.disconnect();
            state = MpMenuState{};
        }

        // Connection status.
        std::string status = net.connected()       ? "Status: connected!"
                             : state.connectingOut ? "Status: waiting for peer..."
                                                   : "Status: share the offer above";
        ui.drawText(status, cx - ui.textWidth(status, 1.4f) / 2.0f, footerY + btnH + 16.0f, 1.4f,
                    glm::vec4(0.85f, 0.95f, 0.85f, 1.0f));
        if (!state.lastError.empty()) {
            ui.drawText(state.lastError, cx - ui.textWidth(state.lastError, 1.3f) / 2.0f, footerY + btnH + 40.0f, 1.3f,
                        glm::vec4(1.0f, 0.5f, 0.4f, 1.0f));
        }
    } else { // Join
        float labelY = panelTopY;
        ui.drawText("Paste host's offer SDP:", cx - 330.0f, labelY, 1.4f);
        widgets.textField(ui, state.offerInput, cx - 330.0f, labelY + 22.0f, 500.0f, 36.0f, "paste offer JSON");
        if (widgets.button(ui, "Paste", cx + 180.0f, labelY + 22.0f, 150.0f, 36.0f)) {
            requestClipboardInto(state.offerInput, 0);
        }

        float genY = labelY + 80.0f;
        if (widgets.button(ui, "Generate answer", cx - btnW, genY, btnW * 2 + 20.0f, 40.0f)) {
            if (!state.offerInput.buffer.empty()) {
                std::string ans = net.acceptOffer(state.offerInput.buffer);
                state.answerInput.buffer = ans;
                state.answerInput.cursor = ans.size();
                state.awaitingLocalSdp = ans.empty();
                state.sessionStarted = true;
                state.connectingOut = true;
            } else {
                state.lastError = "Offer SDP is empty.";
            }
        }

        float ansLabelY = genY + 60.0f;
        ui.drawText("Your answer SDP (send to host):", cx - 330.0f, ansLabelY, 1.4f);
        drawReadonlyArea(ui, cx - 330.0f, ansLabelY + 22.0f, 660.0f, 110.0f, state.answerInput.buffer);
        float copyY = ansLabelY + 140.0f;
        if (widgets.button(ui, "Copy answer", cx - btnW / 2.0f, copyY, btnW, 36.0f)) {
            if (!state.answerInput.buffer.empty()) copyToClipboard(state.answerInput.buffer);
        }

        float footerY = copyY + 60.0f;
        if (widgets.button(ui, "Back", cx - btnW / 2.0f, footerY, btnW, btnH)) {
            net.disconnect();
            state = MpMenuState{};
        }

        std::string status = net.connected()        ? "Status: connected!"
                             : state.sessionStarted ? "Status: waiting for host..."
                                                    : "Status: paste an offer to start";
        ui.drawText(status, cx - ui.textWidth(status, 1.4f) / 2.0f, footerY + btnH + 16.0f, 1.4f,
                    glm::vec4(0.85f, 0.95f, 0.85f, 1.0f));
        if (!state.lastError.empty()) {
            ui.drawText(state.lastError, cx - ui.textWidth(state.lastError, 1.3f) / 2.0f, footerY + btnH + 40.0f, 1.3f,
                        glm::vec4(1.0f, 0.5f, 0.4f, 1.0f));
        }
    }

    widgets.endFrame(ui);
    ui.end();
    return next;
}
