#include "chat_commands.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

ChatCommands::ChatCommands() {
    add("help", "[command]", "List commands, or show one command's usage",
        [this](const Args& args, Reply& out) { help(args, out); });
}

void ChatCommands::add(const std::string& name, const std::string& usage, const std::string& help, Handler handler) {
    commands_.push_back({lower(name), usage, help, std::move(handler)});
}

const ChatCommands::Command* ChatCommands::find(const std::string& name) const {
    std::string key = lower(name);
    if (!key.empty() && key[0] == '/') key.erase(0, 1);
    for (const Command& c : commands_)
        if (c.name == key) return &c;
    return nullptr;
}

bool ChatCommands::parseToggle(const std::string& arg, bool current, bool& out) {
    std::string a = lower(arg);
    if (a.empty()) {
        out = !current;
    } else if (a == "on" || a == "true") {
        out = true;
    } else if (a == "off" || a == "false") {
        out = false;
    } else {
        return false;
    }
    return true;
}

ChatCommands::Reply ChatCommands::run(const std::string& line) const {
    Reply out;
    std::istringstream in(isCommand(line) ? line.substr(1) : line);
    std::string name;
    in >> name;
    Args args;
    for (std::string word; in >> word;) args.push_back(word);

    const Command* cmd = find(name);
    if (!cmd) {
        out.push_back("Unknown command: /" + name + ". Type /help for a list.");
        return out;
    }
    cmd->handler(args, out);
    return out;
}

void ChatCommands::help(const Args& args, Reply& out) const {
    auto synopsis = [](const Command& c) { return "/" + c.name + (c.usage.empty() ? "" : " " + c.usage); };
    if (!args.empty()) {
        const Command* c = find(args[0]);
        if (!c) {
            out.push_back("Unknown command: /" + args[0]);
            return;
        }
        out.push_back(synopsis(*c) + " - " + c->help);
        return;
    }
    out.emplace_back("Commands:");
    for (const Command& c : commands_) out.push_back("  " + synopsis(c) + " - " + c.help);
}
