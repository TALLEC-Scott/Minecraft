#pragma once

// Menu-widget helper layer. Owns the mouse / click / keyboard state the
// primitives all need and offers a small immediate-mode API for the state
// draw functions in menu.cpp to compose from. Adding a new menu screen is
// now "beginFrame → call widgets.button/slider/... → endFrame" — the
// individual primitives, tooltip layering, and click-consumption bookkeeping
// all live here rather than being reimplemented per-screen.

#include "game_state.h"
#include "ui_renderer.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <vector>

// Text-input widget state (used by rename / create-world screens). Kept as
// a POD so callers can own multiple concurrent inputs.
struct TextInput {
    std::string buffer;
    std::size_t cursor = 0;
    std::size_t maxLen = 48;
    bool active = false;
    void setText(const std::string& s) {
        buffer = s.size() > maxLen ? s.substr(0, maxLen) : s;
        cursor = buffer.size();
    }
};

// Per-button options. Kept as a trailing argument with sensible defaults so
// most call sites stay terse; an `enabled`-only overload below covers the
// common case without forcing a brace-init.
struct WidgetOpts {
    bool enabled = true;
    std::string tooltip; // empty = no tooltip
};

class Widgets {
  public:
    // Audio hook — called once per widget click (buttons, toggles, sliders on
    // start-drag). Host plugs in Menu::playClick.
    void setClickSound(std::function<void()> fn) { clickSound = std::move(fn); }

    // Call at the start of every frame before any widget. Samples mouse state
    // and clears per-frame bookkeeping (active slider, pending tooltips).
    void beginFrame(GLFWwindow* window);
    // Call at the end of every frame, AFTER all widgets. Draws pending
    // tooltips so they layer on top, and consumes the one-shot Enter/Esc/Tab
    // latches so callers don't have to clear them by hand.
    void endFrame(UIRenderer& ui);

    // --- Primitives ---
    bool button(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, const WidgetOpts& opts);
    // enabled-only convenience overload (most disabled buttons don't need a tooltip).
    bool button(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool enabled = true) {
        return button(ui, label, x, y, w, h, WidgetOpts{enabled, ""});
    }
    bool slider(UIRenderer& ui, const std::string& label, int sliderID, float x, float y, float w, float h,
                float& value, float minVal, float maxVal, const std::string& suffix = "");
    bool toggle(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool& value);
    // Text-input field. Click inside activates; Enter returns true.
    bool textField(UIRenderer& ui, TextInput& in, float x, float y, float w, float h, const std::string& placeholder);

    // --- Queries ---
    bool hoveredRect(float x, float y, float w, float h) const;
    bool clicked() const { return mouseIsDown && !mouseWasPressed; }
    double mouseX() const { return mx; }
    double mouseY() const { return my; }
    // For keyboard-routed screens (edge-triggered ESC).
    bool escPressed(GLFWwindow* window);
    // For external widgets that need to consume the current click.
    void consumeClick() { clickConsumed = true; }
    // Tell the edge detector that ESC is currently held. Call on Playing→Paused
    // so the pause screen's first escPressed() doesn't fire on the same press
    // that opened it (which would close the menu immediately).
    void markEscHeld() { escWasPressed = true; }

    // --- Text-input key / char routing (from GLFW callbacks) ---
    void onCharInput(unsigned int codepoint);
    void onKeyInput(int key, int action);
    // State-entry hook: drop any latches/pending keys that may have accumulated
    // in non-text states so we don't fire stale Enter/Esc on the first frame.
    void clearInputLatches();
    // Explicit focus set — used by screens that activate a field without a
    // click (initial focus, Tab cycling). Pass nullptr to clear.
    void setFocus(TextInput* in) { focusedInput = in; }

    // One-shot latches (consumed by caller each frame).
    bool enterPressedLatch = false;
    bool escPressedLatch = false;
    bool tabPressedLatch = false;

  private:
    // Mouse
    bool mouseWasPressed = false;
    bool mouseIsDown = false;
    bool clickConsumed = false;
    double mx = 0, my = 0;
    int activeSlider = -1;

    bool escWasPressed = false; // GLFW_PRESS edge detector

    std::vector<int> pendingKeys;
    std::vector<unsigned int> pendingChars;
    std::vector<std::string> pendingTooltips;

    // Click-tracked focus for textField. Writing to any input field clicks
    // its address in here so other textField calls can recognise that they
    // just lost focus and deactivate themselves before consuming input.
    TextInput* focusedInput = nullptr;

    std::function<void()> clickSound;

    // Text-input helper (applies Backspace/Delete/arrows/Home/End queued by
    // onKeyInput). Called from textField.
    void applyPendingKeys(TextInput& in);
};
