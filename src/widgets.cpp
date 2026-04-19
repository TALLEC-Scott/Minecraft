#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

void Widgets::beginFrame(GLFWwindow* window) {
    glfwGetCursorPos(window, &mx, &my);
    bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!down) clickConsumed = false;
    mouseWasPressed = mouseIsDown;
    mouseIsDown = down && !clickConsumed;
    if (!down) activeSlider = -1;
    pendingTooltips.clear();
}

void Widgets::endFrame(UIRenderer& ui) {
    // Chars not consumed by an active textField this frame are discarded so
    // they don't accumulate forever while the cursor is in a non-text menu.
    pendingChars.clear();
    for (const auto& text : pendingTooltips) {
        constexpr float tipScale = 1.4f;
        constexpr float padX = 8.0f, padY = 4.0f;
        float tipW = ui.textWidth(text, tipScale) + padX * 2;
        float tipH = ui.textHeight(tipScale) + padY * 2;
        float tipX = static_cast<float>(mx) + 14.0f;
        float tipY = static_cast<float>(my) + 18.0f;
        ui.drawBorderedRect(tipX, tipY, tipW, tipH, glm::vec4(0.15f, 0.15f, 0.18f, 0.95f),
                            glm::vec4(0.0f, 0.0f, 0.0f, 0.95f));
        ui.drawTextShadow(text, tipX + padX, tipY + padY, tipScale);
    }
    // One-shot latches are consumed here so each screen doesn't have to
    // clear them manually — otherwise stale presses leak across frames.
    enterPressedLatch = false;
    escPressedLatch = false;
    tabPressedLatch = false;
}

bool Widgets::hoveredRect(float x, float y, float w, float h) const {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

bool Widgets::escPressed(GLFWwindow* window) {
#ifdef __EMSCRIPTEN__
    bool down = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
#else
    bool down = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
#endif
    bool pressed = down && !escWasPressed;
    escWasPressed = down;
    return pressed;
}

bool Widgets::button(UIRenderer& ui, const std::string& label, float x, float y, float w, float h,
                     const WidgetOpts& opts) {
    bool overRect = hoveredRect(x, y, w, h);
    bool hovered = opts.enabled && overRect;
    bool didClick = hovered && clicked();
    if (didClick) {
        if (clickSound) clickSound();
        clickConsumed = true;
    }

    glm::vec4 bgColor;
    if (!opts.enabled)
        bgColor = glm::vec4(0.18f, 0.18f, 0.20f, 0.85f);
    else if (hovered)
        bgColor = glm::vec4(0.45f, 0.45f, 0.55f, 0.85f);
    else
        bgColor = glm::vec4(0.25f, 0.25f, 0.30f, 0.85f);

    ui.drawBorderedRect(x, y, w, h, bgColor);
    ui.drawRect(x, y, w, 1, glm::vec4(0.6f, 0.6f, 0.7f, 0.5f));

    float scale = 2.0f;
    float tw = ui.textWidth(label, scale);
    float th = ui.textHeight(scale);
    float tx = x + (w - tw) / 2.0f;
    float ty = y + (h - th) / 2.0f;
    glm::vec4 textColor = opts.enabled ? glm::vec4(1.0f) : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    ui.drawTextShadow(label, tx, ty, scale, textColor);

    // Tooltips fire even when disabled — "why is this greyed out?" is exactly
    // when the user wants the hint. Queued here and drawn in endFrame so the
    // tooltip always layers on top of later widgets.
    if (!opts.tooltip.empty() && overRect) pendingTooltips.push_back(opts.tooltip);
    return didClick;
}

bool Widgets::slider(UIRenderer& ui, const std::string& label, int sliderID, float x, float y, float w, float h,
                     float& value, float minVal, float maxVal, const std::string& suffix) {
    bool changed = false;
    if (activeSlider == sliderID && mouseIsDown) {
        float t = (static_cast<float>(mx) - x) / w;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        changed = true;
    }

    bool hovered = hoveredRect(x, y, w, h);
    if (hovered && mouseIsDown && activeSlider == -1) {
        activeSlider = sliderID;
        float t = (static_cast<float>(mx) - x) / w;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        changed = true;
    }

    ui.drawBorderedRect(x, y, w, h, glm::vec4(0.2f, 0.2f, 0.25f, 0.85f));

    float t = (value - minVal) / (maxVal - minVal);
    ui.drawRect(x, y, w * t, h, glm::vec4(0.35f, 0.55f, 0.35f, 0.85f));
    ui.drawRect(x + w * t - 3, y, 6, h, glm::vec4(0.9f, 0.9f, 0.9f, 0.9f));

    // snprintf into a fixed buffer — cheaper than stringstream on the 60 FPS
    // path (Settings has 4 sliders each allocating a stringstream per frame).
    char buf[96];
    if (maxVal - minVal > 10.0f && minVal >= 0.0f) {
        std::snprintf(buf, sizeof(buf), "%s: %d%s", label.c_str(), static_cast<int>(value), suffix.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s: %.1f%s", label.c_str(), value, suffix.c_str());
    }
    float scale = 2.0f;
    float tw = ui.textWidth(buf, scale);
    float th = ui.textHeight(scale);
    ui.drawTextShadow(buf, x + (w - tw) / 2.0f, y + (h - th) / 2.0f, scale);
    return changed;
}

bool Widgets::toggle(UIRenderer& ui, const std::string& label, float x, float y, float w, float h, bool& value) {
    bool hovered = hoveredRect(x, y, w, h);
    bool didClick = hovered && clicked();
    if (didClick) {
        value = !value;
        if (clickSound) clickSound();
    }

    glm::vec4 bgColor = value ? glm::vec4(0.35f, 0.55f, 0.35f, 0.85f) : glm::vec4(0.4f, 0.2f, 0.2f, 0.85f);
    if (hovered) bgColor += glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);

    ui.drawBorderedRect(x, y, w, h, bgColor);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s: %s", label.c_str(), value ? "ON" : "OFF");
    float scale = 2.0f;
    float tw = ui.textWidth(buf, scale);
    float th = ui.textHeight(scale);
    ui.drawTextShadow(buf, x + (w - tw) / 2.0f, y + (h - th) / 2.0f, scale);
    return didClick;
}

bool Widgets::textField(UIRenderer& ui, TextInput& in, float x, float y, float w, float h,
                        const std::string& placeholder) {
    bool hovered = hoveredRect(x, y, w, h);
    if (hovered && clicked()) {
        if (focusedInput && focusedInput != &in) focusedInput->active = false;
        focusedInput = &in;
        in.active = true;
        in.cursor = in.buffer.size();
        if (clickSound) clickSound();
        clickConsumed = true;
    }

    // Another field got focus this frame — let go of input.
    if (focusedInput && focusedInput != &in) in.active = false;

    if (in.active) {
        applyPendingKeys(in);
        for (unsigned int cp : pendingChars) {
            if (cp < 0x20 || cp > 0x7E) continue; // bitmap font is printable-ASCII only
            if (in.buffer.size() >= in.maxLen) break;
            in.buffer.insert(in.buffer.begin() + in.cursor, static_cast<char>(cp));
            in.cursor++;
        }
        pendingChars.clear();
    }

    glm::vec4 border = in.active ? glm::vec4(0.9f, 0.9f, 0.4f, 1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 0.9f);
    ui.drawBorderedRect(x, y, w, h, glm::vec4(0.12f, 0.12f, 0.14f, 0.95f), border);

    float scale = 2.0f;
    float padding = 8.0f;
    float textY = y + (h - ui.textHeight(scale)) / 2.0f;
    bool showPlaceholder = in.buffer.empty() && !in.active;
    const std::string& shown = showPlaceholder ? placeholder : in.buffer;
    glm::vec4 textColor = showPlaceholder ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : glm::vec4(1.0f);
    ui.drawText(shown, x + padding, textY, scale, textColor);

    if (in.active) {
        bool visible = std::fmod(glfwGetTime(), 1.0) < 0.5;
        if (visible) {
            // UIRenderer's font is a fixed-width 8 px bitmap, so caret x is a
            // multiply — no need to allocate a substring just to measure it.
            float caretX = x + padding + static_cast<float>(in.cursor) * UIRenderer::GLYPH_W * scale;
            float caretH = ui.textHeight(scale);
            ui.drawRect(caretX, textY, 2, caretH, glm::vec4(1.0f));
        }
    }
    return in.active && enterPressedLatch;
}

void Widgets::onCharInput(unsigned int codepoint) {
    pendingChars.push_back(codepoint);
}

void Widgets::onKeyInput(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) enterPressedLatch = true;
    else if (key == GLFW_KEY_ESCAPE) escPressedLatch = true;
    else if (key == GLFW_KEY_TAB) tabPressedLatch = true;
    else pendingKeys.push_back(key);
}

void Widgets::clearInputLatches() {
    enterPressedLatch = false;
    escPressedLatch = false;
    tabPressedLatch = false;
    pendingKeys.clear();
    pendingChars.clear();
}

void Widgets::applyPendingKeys(TextInput& in) {
    for (int key : pendingKeys) {
        switch (key) {
            case GLFW_KEY_BACKSPACE:
                if (in.cursor > 0) {
                    in.buffer.erase(in.buffer.begin() + (in.cursor - 1));
                    in.cursor--;
                }
                break;
            case GLFW_KEY_DELETE:
                if (in.cursor < in.buffer.size()) in.buffer.erase(in.buffer.begin() + in.cursor);
                break;
            case GLFW_KEY_LEFT:
                if (in.cursor > 0) in.cursor--;
                break;
            case GLFW_KEY_RIGHT:
                if (in.cursor < in.buffer.size()) in.cursor++;
                break;
            case GLFW_KEY_HOME:
                in.cursor = 0;
                break;
            case GLFW_KEY_END:
                in.cursor = in.buffer.size();
                break;
        }
    }
    pendingKeys.clear();
}
