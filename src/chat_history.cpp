#include "chat_history.h"

#include <fstream>
#include <utility>

ChatHistory::ChatHistory(std::string path) : path_(std::move(path)) {}

void ChatHistory::load() {
    entries_.clear();
    if (!path_.empty()) {
        std::ifstream in(path_);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back(); // file edited on Windows
            if (!line.empty()) entries_.push_back(line);
        }
        if (entries_.size() > MAX_LINES)
            entries_.erase(entries_.begin(), entries_.end() - static_cast<std::ptrdiff_t>(MAX_LINES));
    }
    resetNavigation();
}

void ChatHistory::add(const std::string& line) {
    if (!line.empty() && line.find('\n') == std::string::npos && (entries_.empty() || entries_.back() != line)) {
        entries_.push_back(line);
        if (entries_.size() > MAX_LINES) entries_.erase(entries_.begin());
        save();
    }
    resetNavigation();
}

bool ChatHistory::older(const std::string& current, std::string& out) {
    if (cursor_ == 0) return false;
    if (cursor_ == entries_.size()) draft_ = current;
    --cursor_;
    out = entries_[cursor_];
    return true;
}

bool ChatHistory::newer(std::string& out) {
    if (cursor_ >= entries_.size()) return false;
    ++cursor_;
    out = (cursor_ == entries_.size()) ? draft_ : entries_[cursor_];
    return true;
}

void ChatHistory::resetNavigation() {
    cursor_ = entries_.size();
    draft_.clear();
}

void ChatHistory::save() const {
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    for (const std::string& e : entries_) out << e << '\n';
}
