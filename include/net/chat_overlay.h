#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class ChatHistory;
class UIRenderer;
struct GLFWwindow;

// Minecraft-style in-game chat. Press `T` or Enter to open an input row at
// the bottom of the screen (`/` opens it with the slash typed, for
// commands), type a line, press Enter to send (Esc to cancel).
// Incoming lines (local + remote) accumulate in a log panel that fades
// after a few seconds when chat is closed. Rendering + input handling
// share the same UIRenderer font used by widgets.cpp.
class ChatOverlay {
  public:
    bool isOpen() const { return open_; }
    // Up / Down in the input walk this history (not owned; may be null).
    void setHistory(ChatHistory* history) { history_ = history; }
    // `prefill` seeds the input (e.g. "/" when opened by the slash key).
    void open(const std::string& prefill = "");
    void close();

    // Wired to the GLFW char / key callbacks. Both no-op when chat is
    // closed, so they're safe to route unconditionally.
    void onCharInput(unsigned int codepoint);
    void onKeyInput(GLFWwindow* window, int key, int action, int mods);

    // Append a line to the log. Local messages render as `<you> ...`,
    // remotes as `<peer NNNN> ...` where NNNN is the last 4 hex digits
    // of peerId.
    void pushLocalMessage(const std::string& text);
    void pushRemoteMessage(uint32_t peerId, const std::string& text);
    // Command replies: no sender tag, drawn in yellow.
    void pushSystemMessage(const std::string& text);

    // Drain pending input. If Enter was pressed this frame and the
    // buffer is non-empty, writes the trimmed text into `out`, clears
    // the buffer, closes the overlay, and returns true. Otherwise
    // returns false (and still applies Esc / Backspace / cursor edits
    // so they're reflected in the next draw).
    bool takePendingSend(std::string& out);

    // Panel + input row. Call once per frame inside a UIRenderer begin/end.
    void draw(UIRenderer& ui, int windowW, int windowH, double now);

  private:
    struct LogLine {
        std::string text;
        double recvTime = 0.0;
        bool system = false;
    };

    void pushLine(std::string text, bool system);

    ChatHistory* history_ = nullptr;
    bool open_ = false;
    std::string buffer_;
    std::size_t cursor_ = 0;
    std::deque<LogLine> log_;
    std::vector<unsigned int> pendingChars_;
    std::vector<int> pendingKeys_;
    bool enterLatched_ = false;
    bool escLatched_ = false;
};
