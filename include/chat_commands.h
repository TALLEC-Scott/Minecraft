#pragma once

// Slash commands typed into the chat overlay ("/help", "/time day", ...).
// A line starting with '/' is a command: it runs locally and is never sent
// to the peer. Commands are registered with a handler that appends reply
// lines; /help is built in and lists every registered command. GL-free so
// the parser is unit-tested.

#include <functional>
#include <string>
#include <vector>

class ChatCommands {
  public:
    using Args = std::vector<std::string>;  // words after the command name
    using Reply = std::vector<std::string>; // lines shown in the chat log
    using Handler = std::function<void(const Args& args, Reply& out)>;

    ChatCommands();

    // `name` without the slash, matched case-insensitively. `usage` is the
    // argument synopsis ("<day|night>"), `help` a one-line description.
    void add(const std::string& name, const std::string& usage, const std::string& help, Handler handler);

    static bool isCommand(const std::string& line) { return !line.empty() && line[0] == '/'; }

    // Run a command line (leading '/' included) and return its reply.
    // Unknown commands reply with a pointer to /help.
    Reply run(const std::string& line) const;

  private:
    struct Command {
        std::string name, usage, help;
        Handler handler;
    };
    std::vector<Command> commands_;

    const Command* find(const std::string& name) const;
    void help(const Args& args, Reply& out) const;
};
