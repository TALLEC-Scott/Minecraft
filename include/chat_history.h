#pragma once

// Bash-style history for the chat input: Up/Down walk through previously
// sent lines (messages and /commands alike), newest first. Persisted one
// entry per in a text file capped at MAX_LINES (oldest dropped, like
// HISTFILESIZE). Consecutive duplicates are stored once (HISTCONTROL=
// ignoredups). While browsing, the half-typed line is kept as a draft and
// comes back when you step past the newest entry. GL-free for unit tests.

#include <cstddef>
#include <string>
#include <vector>

class ChatHistory {
  public:
    static constexpr std::size_t MAX_LINES = 1000;

    // Empty path → in-memory only (tests, or no writable location).
    explicit ChatHistory(std::string path = "");

    // Replace the in-memory history with the file's contents (newest last).
    // A missing file is an empty history.
    void load();
    // Record a sent line, rewrite the file (trimmed to MAX_LINES), and
    // end any browsing. Blank lines and repeats of the last entry are not
    // stored again.
    void add(const std::string& line);

    // Navigation. `current` is the input text at the moment of the key
    // press; the first older() call saves it as the draft. Each returns the
    // text to show, or false when there's nothing further in that direction.
    bool older(const std::string& current, std::string& out);
    bool newer(std::string& out);
    // Forget the browse position (chat opened / closed).
    void resetNavigation();

    const std::vector<std::string>& entries() const { return entries_; }

  private:
    std::string path_;
    std::vector<std::string> entries_; // oldest first
    // Index into entries_ being shown; entries_.size() = not browsing.
    std::size_t cursor_ = 0;
    std::string draft_;

    void save() const;
};
