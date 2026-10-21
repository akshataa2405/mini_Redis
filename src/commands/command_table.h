#pragma once

#include "../protocol/resp_parser.h"
#include "../store/store.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace miniredis {

using CommandHandler = std::function<std::string(const Command&, Store&)>;

struct CommandDef {
    std::string name;
    int min_args; // including command name itself
    int max_args; // -1 for unlimited
    bool is_write;
    CommandHandler handler;
};

class CommandTable {
public:
    CommandTable();

    std::string execute(const Command& cmd, Store& store);

private:
    std::unordered_map<std::string, CommandDef> registry_;

    void register_command(std::string name, int min_args, int max_args, bool is_write, CommandHandler handler);
    void register_all();
};

} // namespace miniredis
