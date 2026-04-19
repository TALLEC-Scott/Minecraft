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

// Pick the signaling-server URL. On web the server lives at the same
// origin under /signal; browsers served over HTTPS need wss:// to avoid
// mixed-content blocking. Localhost dev tests use ws://.
#ifdef __EMSCRIPTEN__
EM_JS(const char*, sig_server_url, (), {
    // Prod (HTTPS): Caddy/nginx proxies wss://host[:port]/signal to the
    // node process. Use location.host (includes the port) so a non-default
    // HTTPS port like :7862 routes correctly. Dev (plain HTTP, typically
    // python3 -m http.server): connect straight to the signaling process
    // on :7863 — no reverse proxy needed.
    var url;
    if (location.protocol === "https:") {
        url = "wss://" + location.host + "/signal";
    } else {
        url = "ws://" + location.hostname + ":7863";
    }
    var bytes = lengthBytesUTF8(url) + 1;
    var ptr = _malloc(bytes);
    stringToUTF8(url, ptr, bytes);
    return ptr;
});
#endif

std::string signalingServerUrl() {
#ifdef __EMSCRIPTEN__
    const char* p = sig_server_url();
    if (!p) return "";
    std::string out(p);
    std::free((void*)p);
    return out;
#else
    return "";
#endif
}

// Copy a string into the browser clipboard (web only). Relies on the
// async Clipboard API, which needs a secure context — the site serves
// over HTTPS so this is always available. No-op on desktop.
void copyToClipboard(const std::string& s) {
#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_ASM({
        var text = UTF8ToString($0);
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(text).catch(function(e) { console.warn("clipboard", e); });
        }
    }, s.c_str());
    // clang-format on
#else
    (void)s;
#endif
}

// Read the browser clipboard directly on explicit button click. This path
// triggers Firefox's one-time permission prompt (small "Paste" button near
// the URL bar); once granted, subsequent clicks work without prompting.
// Ctrl+V is handled separately in the widget layer via a document paste
// listener, which doesn't require this permission.
#ifdef __EMSCRIPTEN__
EM_JS(void, mpmenu_request_clipboard, (int slot), {
    if (!navigator.clipboard || !navigator.clipboard.readText) return;
    navigator.clipboard.readText()
        .then(function(text) {
            if (!text) return;
            if (!Module._mpClipboard) Module._mpClipboard = {};
            Module._mpClipboard[slot] = text;
        })
        .catch(function(e) { console.warn("clipboard read", e); });
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
#endif

void pasteInto(TextInput& in, int slot) {
#ifdef __EMSCRIPTEN__
    mpmenu_request_clipboard(slot);
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
    std::string raw(p);
    std::free((void*)p);
    // Drop control chars (newlines, tabs) that OS-level clipboards sometimes
    // insert between JSON lines — the bitmap font renders them as gap-sized
    // blank glyphs, which looks like extra spaces in the pasted SDP.
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (char c : raw) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7F) cleaned += c;
    }
    if (cleaned.size() > in.maxLen) cleaned.resize(in.maxLen);
    in.buffer = cleaned;
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

    // Drain any clipboard reads triggered by the Paste button — the async
    // browser read usually resolves within a frame of clicking it.
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
        // Quick (room-code) flow — preferred for most users.
        std::string quickHeader = "Quick connect (room code):";
        ui.drawText(quickHeader, cx - ui.textWidth(quickHeader, 1.4f) / 2.0f, panelTopY, 1.4f,
                    glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
        float gap = 40.0f;
        float row1X = cx - btnW - gap / 2.0f;
        float row2X = cx + gap / 2.0f;
        if (widgets.button(ui, "Create room", row1X, panelTopY + 26.0f, btnW, btnH)) {
            state.panel = MpPanel::QuickHost;
            net.startHostSignaling(signalingServerUrl());
            state.sessionStarted = true;
            state.connectingOut = true;
        }
        if (widgets.button(ui, "Join room", row2X, panelTopY + 26.0f, btnW, btnH)) {
            state.panel = MpPanel::QuickJoin;
            state.codeInput.setText("");
            state.codeInput.active = true;
            state.sessionStarted = false;
        }

        // Manual (copy-paste SDP) fallback.
        float manualY = panelTopY + 120.0f;
        std::string manualHeader = "Manual signaling (advanced):";
        ui.drawText(manualHeader, cx - ui.textWidth(manualHeader, 1.4f) / 2.0f, manualY, 1.4f,
                    glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
        if (widgets.button(ui, "Host", row1X, manualY + 24.0f, btnW, btnH)) {
            state.panel = MpPanel::Host;
            std::string sdp = net.createOffer();
            state.offerInput.buffer = sdp;
            state.offerInput.cursor = sdp.size();
            state.awaitingLocalSdp = sdp.empty();
            state.sessionStarted = true;
        }
        if (widgets.button(ui, "Join", row2X, manualY + 24.0f, btnW, btnH)) {
            state.panel = MpPanel::Join;
            state.sessionStarted = false;
        }

        std::string hint = "Quick connect uses a tiny signaling relay to swap connection info. "
                           "Manual SDP is useful when the relay is down - host generates an offer, "
                           "peer pastes it and returns an answer, host pastes that back.";
        float s = 1.2f;
        float hintWidth = std::min(static_cast<float>(windowW) - 40.0f, 640.0f);
        drawWrappedText(ui, hint, cx - hintWidth / 2.0f, manualY + 90.0f, hintWidth, s,
                        glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));

        float backX = cx - btnW / 2.0f;
        if (widgets.button(ui, "Back", backX, cy * 0.88f, btnW, btnH)) {
            next = GameState::MainMenu;
            state = MpMenuState{};
        }
        if (widgets.escPressed(window)) {
            next = GameState::MainMenu;
            state = MpMenuState{};
        }
    } else if (state.panel == MpPanel::QuickHost) {
        float y = panelTopY;
        std::string title2 = "Quick connect - Host";
        ui.drawTextShadow(title2, cx - ui.textWidth(title2, 1.8f) / 2.0f, y, 1.8f);
        y += 50.0f;
        std::string code = net.signalingCode();
        if (code.empty()) {
            std::string wait = "Contacting signaling server...";
            ui.drawText(wait, cx - ui.textWidth(wait, 1.4f) / 2.0f, y, 1.4f,
                        glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
        } else {
            std::string label = "Share this code with your peer:";
            ui.drawText(label, cx - ui.textWidth(label, 1.3f) / 2.0f, y, 1.3f,
                        glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
            ui.drawTextShadow(code, cx - ui.textWidth(code, 4.0f) / 2.0f, y + 30.0f, 4.0f,
                              glm::vec4(1.0f, 1.0f, 0.5f, 1.0f));
            if (widgets.button(ui, "Copy code", cx - btnW / 2.0f, y + 100.0f, btnW, 36.0f)) {
                copyToClipboard(code);
            }
        }
        std::string status = net.signalingStatus();
        if (!status.empty()) {
            ui.drawText("Status: " + status, cx - ui.textWidth("Status: " + status, 1.3f) / 2.0f,
                        y + 160.0f, 1.3f, glm::vec4(0.75f, 0.9f, 0.75f, 1.0f));
        }
        if (widgets.button(ui, "Back", cx - btnW / 2.0f, cy * 0.85f, btnW, btnH)) {
            net.disconnect();
            state = MpMenuState{};
        }
        if (widgets.escPressed(window)) {
            net.disconnect();
            state = MpMenuState{};
        }
    } else if (state.panel == MpPanel::QuickJoin) {
        float y = panelTopY;
        std::string title2 = "Quick connect - Join";
        ui.drawTextShadow(title2, cx - ui.textWidth(title2, 1.8f) / 2.0f, y, 1.8f);
        y += 50.0f;
        ui.drawText("Enter room code:", cx - 220.0f, y, 1.4f);
        widgets.textField(ui, state.codeInput, cx - 220.0f, y + 22.0f, 440.0f, 44.0f, "e.g. ABC234");
        if (widgets.button(ui, "Connect", cx - btnW / 2.0f, y + 90.0f, btnW, btnH)) {
            if (!state.codeInput.buffer.empty()) {
                // Normalize to uppercase so users who type lowercase still match.
                std::string code = state.codeInput.buffer;
                for (auto& c : code) c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
                net.startJoinSignaling(signalingServerUrl(), code);
                state.sessionStarted = true;
                state.connectingOut = true;
            } else {
                state.lastError = "Enter a room code.";
            }
        }
        std::string status = net.signalingStatus();
        if (!status.empty()) {
            ui.drawText("Status: " + status, cx - ui.textWidth("Status: " + status, 1.3f) / 2.0f,
                        y + 150.0f, 1.3f, glm::vec4(0.75f, 0.9f, 0.75f, 1.0f));
        }
        if (!state.lastError.empty()) {
            ui.drawText(state.lastError, cx - ui.textWidth(state.lastError, 1.3f) / 2.0f, y + 175.0f,
                        1.3f, glm::vec4(1.0f, 0.5f, 0.4f, 1.0f));
        }
        if (widgets.button(ui, "Back", cx - btnW / 2.0f, cy * 0.85f, btnW, btnH)) {
            net.disconnect();
            state = MpMenuState{};
        }
        if (widgets.escPressed(window)) {
            net.disconnect();
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
        ui.drawText("Paste peer's answer below (Ctrl+V recommended):", cx - 330.0f, ansY, 1.4f);
        widgets.textField(ui, state.answerInput, cx - 330.0f, ansY + 22.0f, 500.0f, 36.0f, "Ctrl+V to paste answer JSON");
        if (widgets.button(ui, "Paste", cx + 180.0f, ansY + 22.0f, 150.0f, 36.0f)) {
            pasteInto(state.answerInput, 1);
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
        ui.drawText("Paste host's offer SDP (Ctrl+V recommended):", cx - 330.0f, labelY, 1.4f);
        widgets.textField(ui, state.offerInput, cx - 330.0f, labelY + 22.0f, 500.0f, 36.0f, "Ctrl+V to paste offer JSON");
        if (widgets.button(ui, "Paste", cx + 180.0f, labelY + 22.0f, 150.0f, 36.0f)) {
            pasteInto(state.offerInput, 0);
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
