#include "net/chat_overlay.h"

#include "net/net_protocol.h"
#include "ui_renderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr std::size_t MAX_LOG_LINES = 64;
// Lines stay fully visible for this long after arriving, then fade out
// over FADE_SECS. Matches the "chat goes away if you don't look at it"
// behavior in the vanilla game.
constexpr double VISIBLE_SECS = 8.0;
constexpr double FADE_SECS = 1.0;

// Greedy word-wrap against a pixel budget. Duplicated here rather than
// shared with multiplayer_menu's drawWrappedText because the scrollback
// needs the wrapped lines as strings (for reverse-order rendering and
// line counting), not just a rendered height.
std::vector<std::string> wrapLines(const std::string& text, std::size_t maxChars) {
    std::vector<std::string> out;
    if (maxChars == 0) maxChars = 1;
    std::string line;
    std::size_t i = 0;
    auto flush = [&]() {
        if (!line.empty()) {
            out.push_back(line);
            line.clear();
        }
    };
    while (i < text.size()) {
        std::size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(i, end - i);
        i = (end < text.size()) ? end + 1 : end;
        if (word.empty()) continue;
        if (line.empty()) {
            if (word.size() > maxChars) {
                // Single word longer than the budget — split it.
                for (std::size_t p = 0; p < word.size(); p += maxChars) {
                    out.push_back(word.substr(p, maxChars));
                }
            } else {
                line = word;
            }
        } else if (line.size() + 1 + word.size() <= maxChars) {
            line += ' ';
            line += word;
        } else {
            flush();
            if (word.size() > maxChars) {
                for (std::size_t p = 0; p < word.size(); p += maxChars) {
                    out.push_back(word.substr(p, maxChars));
                }
            } else {
                line = word;
            }
        }
    }
    flush();
    if (out.empty()) out.emplace_back();
    return out;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

} // namespace

void ChatOverlay::open() {
    open_ = true;
    buffer_.clear();
    cursor_ = 0;
    pendingChars_.clear();
    pendingKeys_.clear();
    // Don't clear latches — a press that arrives mid-frame should still
    // reach takePendingSend().
}

void ChatOverlay::close() {
    open_ = false;
    buffer_.clear();
    cursor_ = 0;
    pendingChars_.clear();
    pendingKeys_.clear();
    enterLatched_ = false;
    escLatched_ = false;
}

void ChatOverlay::onCharInput(unsigned int codepoint) {
    if (!open_) return;
    // Bitmap font is printable ASCII only — same filter as widgets.cpp.
    if (codepoint < 0x20 || codepoint > 0x7E) return;
    pendingChars_.push_back(codepoint);
}

void ChatOverlay::onKeyInput(GLFWwindow* window, int key, int action, int mods) {
    if (!open_) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        enterLatched_ = true;
    } else if (key == GLFW_KEY_ESCAPE) {
        escLatched_ = true;
    } else if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
#ifndef __EMSCRIPTEN__
        const char* cb = glfwGetClipboardString(window);
        if (cb) {
            for (const char* p = cb; *p; ++p) {
                unsigned char c = static_cast<unsigned char>(*p);
                if (c >= 0x20 && c < 0x7F) pendingChars_.push_back(c);
            }
        }
#else
        (void)window;
#endif
    } else {
        pendingKeys_.push_back(key);
    }
}

void ChatOverlay::pushLocalMessage(const std::string& text) {
    LogLine l;
    l.text = "<you> " + text;
    l.recvTime = glfwGetTime();
    log_.push_back(std::move(l));
    while (log_.size() > MAX_LOG_LINES) log_.pop_front();
}

void ChatOverlay::pushRemoteMessage(uint32_t peerId, const std::string& text) {
    char tag[16];
    std::snprintf(tag, sizeof(tag), "<peer %04X> ", peerId & 0xFFFFu);
    LogLine l;
    l.text = std::string(tag) + text;
    l.recvTime = glfwGetTime();
    log_.push_back(std::move(l));
    while (log_.size() > MAX_LOG_LINES) log_.pop_front();
}

bool ChatOverlay::takePendingSend(std::string& out) {
    if (!open_) {
        enterLatched_ = false;
        escLatched_ = false;
        pendingChars_.clear();
        pendingKeys_.clear();
        return false;
    }
    // Apply edit keys first so the user sees the result on the same frame.
    for (int key : pendingKeys_) {
        switch (key) {
        case GLFW_KEY_BACKSPACE:
            if (cursor_ > 0) {
                buffer_.erase(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_ - 1));
                --cursor_;
            }
            break;
        case GLFW_KEY_DELETE:
            if (cursor_ < buffer_.size()) {
                buffer_.erase(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_));
            }
            break;
        case GLFW_KEY_LEFT:
            if (cursor_ > 0) --cursor_;
            break;
        case GLFW_KEY_RIGHT:
            if (cursor_ < buffer_.size()) ++cursor_;
            break;
        case GLFW_KEY_HOME: cursor_ = 0; break;
        case GLFW_KEY_END: cursor_ = buffer_.size(); break;
        }
    }
    pendingKeys_.clear();

    for (unsigned int cp : pendingChars_) {
        if (buffer_.size() >= netp::CHAT_MAX_TEXT_LEN) break;
        buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_), static_cast<char>(cp));
        ++cursor_;
    }
    pendingChars_.clear();

    if (escLatched_) {
        escLatched_ = false;
        enterLatched_ = false;
        close();
        return false;
    }
    if (enterLatched_) {
        enterLatched_ = false;
        std::string trimmed = trim(buffer_);
        if (trimmed.empty()) {
            close();
            return false;
        }
        out = trimmed;
        close();
        return true;
    }
    return false;
}

void ChatOverlay::draw(UIRenderer& ui, int windowW, int windowH, double now) {
    const float scale = 2.0f;
    const float glyphW = UIRenderer::GLYPH_W * scale;
    const float rowH = ui.textHeight(scale) + 4.0f;

    const float panelW = std::min(static_cast<float>(windowW) - 16.0f, 560.0f);
    const float panelX = 8.0f;
    const float panelPad = 6.0f;

    // Pull up recent lines (all of them while open, most-recent when closed).
    const std::size_t visibleCap = open_ ? 10u : 10u;
    std::vector<std::size_t> showIdx;
    for (std::size_t i = log_.size(); i-- > 0 && showIdx.size() < visibleCap;) {
        double age = now - log_[i].recvTime;
        if (!open_ && age > VISIBLE_SECS + FADE_SECS) break;
        showIdx.push_back(i);
    }
    std::reverse(showIdx.begin(), showIdx.end());

    if (showIdx.empty() && !open_) return;

    // Flatten into wrapped rows bottom-up.
    std::size_t wrapCols = static_cast<std::size_t>((panelW - panelPad * 2.0f) / glyphW);
    if (wrapCols < 8) wrapCols = 8;
    struct Row {
        std::string text;
        float alpha;
    };
    std::vector<Row> rows;
    for (std::size_t i : showIdx) {
        const LogLine& l = log_[i];
        double age = now - l.recvTime;
        float alpha = 1.0f;
        if (!open_) {
            if (age > VISIBLE_SECS) {
                float t = static_cast<float>((age - VISIBLE_SECS) / FADE_SECS);
                alpha = 1.0f - std::min(std::max(t, 0.0f), 1.0f);
            }
        }
        auto wrapped = wrapLines(l.text, wrapCols);
        for (auto& w : wrapped) rows.push_back({std::move(w), alpha});
    }

    const float inputRowH = rowH + 6.0f;
    // Panel covers the shown rows + optional input row; anchor to the
    // bottom above the hotbar.
    const float hotbarMargin = 72.0f;
    const float logH = rowH * static_cast<float>(std::min(rows.size(), visibleCap));
    const float panelH = logH + panelPad * 2.0f;
    const float panelY = static_cast<float>(windowH) - hotbarMargin - panelH - (open_ ? inputRowH + 4.0f : 0.0f);

    if (!rows.empty()) {
        if (open_) {
            ui.drawBorderedRect(panelX, panelY, panelW, panelH, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f),
                                glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
        }
        // Draw rows bottom-up so older lines appear above newer ones.
        float y = panelY + panelH - panelPad - rowH;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            glm::vec4 color(1.0f, 1.0f, 1.0f, it->alpha);
            if (open_) {
                ui.drawText(it->text, panelX + panelPad, y, scale, color);
            } else {
                // Closed: draw shadowed text against the world (no panel).
                ui.drawTextShadow(it->text, panelX + panelPad, y, scale, color);
            }
            y -= rowH;
            if (y < panelY + panelPad) break;
        }
    }

    if (open_) {
        const float inputY = panelY + panelH + 4.0f;
        ui.drawBorderedRect(panelX, inputY, panelW, inputRowH, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f),
                            glm::vec4(0.9f, 0.9f, 0.4f, 1.0f));
        const std::string prompt = "> ";
        ui.drawText(prompt + buffer_, panelX + panelPad, inputY + 3.0f, scale);
        bool caretVisible = std::fmod(glfwGetTime(), 1.0) < 0.5;
        if (caretVisible) {
            float caretX = panelX + panelPad + (static_cast<float>(prompt.size()) + static_cast<float>(cursor_)) * glyphW;
            ui.drawRect(caretX, inputY + 3.0f, 2.0f, ui.textHeight(scale), glm::vec4(1.0f));
        }
    }
}
