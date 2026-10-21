#include "command_table.h"
#include "../protocol/resp_encoder.h"
#include "../util/logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace miniredis {

static std::string to_upper(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) {
        res.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return res;
}

CommandTable::CommandTable() {
    register_all();
}

void CommandTable::register_command(std::string name, int min_args, int max_args, bool is_write, CommandHandler handler) {
    std::string upper_name = to_upper(name);
    CommandDef def;
    def.name = upper_name;
    def.min_args = min_args;
    def.max_args = max_args;
    def.is_write = is_write;
    def.handler = std::move(handler);
    registry_[upper_name] = std::move(def);
}

std::string CommandTable::execute(const Command& cmd, Store& store) {
    if (cmd.empty()) {
        return RespEncoder::error("empty command");
    }

    std::string cmd_name = to_upper(cmd[0]);
    auto it = registry_.find(cmd_name);
    if (it == registry_.end()) {
        return RespEncoder::error("unknown command '" + cmd[0] + "'");
    }

    const CommandDef& def = it->second;
    int argc = static_cast<int>(cmd.size());
    if (argc < def.min_args || (def.max_args != -1 && argc > def.max_args)) {
        return RespEncoder::error("wrong number of arguments for '" + cmd[0] + "' command");
    }

    return def.handler(cmd, store);
}

void CommandTable::register_all() {
    // PING [msg]
    register_command("PING", 1, 2, false, [](const Command& cmd, Store&) -> std::string {
        if (cmd.size() == 1) {
            return RespEncoder::pong();
        }
        return RespEncoder::bulk_string(cmd[1]);
    });

    // SET key value [EX seconds] [PX ms] [NX] [XX]
    register_command("SET", 3, 8, true, [](const Command& cmd, Store& store) -> std::string {
        const std::string& key = cmd[1];
        const std::string& val_str = cmd[2];

        int64_t expire_at_ms = 0;
        bool nx = false;
        bool xx = false;

        for (size_t i = 3; i < cmd.size(); ++i) {
            std::string opt = to_upper(cmd[i]);
            if (opt == "EX" && i + 1 < cmd.size()) {
                long sec = std::strtol(cmd[++i].c_str(), nullptr, 10);
                if (sec <= 0) return RespEncoder::error("invalid expire time in 'set' command");
                expire_at_ms = now_ms() + sec * 1000;
            } else if (opt == "PX" && i + 1 < cmd.size()) {
                long ms = std::strtol(cmd[++i].c_str(), nullptr, 10);
                if (ms <= 0) return RespEncoder::error("invalid expire time in 'set' command");
                expire_at_ms = now_ms() + ms;
            } else if (opt == "NX") {
                nx = true;
            } else if (opt == "XX") {
                xx = true;
            } else {
                return RespEncoder::syntax_error();
            }
        }

        if (nx && xx) {
            return RespEncoder::syntax_error();
        }

        Value v(val_str, expire_at_ms);
        SetResult res = store.set(key, std::move(v), nx, xx);
        if (res == SetResult::CONDITION_FAILED) {
            return RespEncoder::null_bulk();
        }
        return RespEncoder::ok();
    });

    // GET key
    register_command("GET", 2, 2, false, [](const Command& cmd, Store& store) -> std::string {
        Value* v = store.get(cmd[1]);
        if (!v) {
            return RespEncoder::null_bulk();
        }
        if (v->type != ValueType::STRING && v->type != ValueType::INTEGER) {
            return RespEncoder::wrong_type();
        }
        return RespEncoder::bulk_string(v->str_val);
    });

    // DEL key [key...]
    register_command("DEL", 2, -1, true, [](const Command& cmd, Store& store) -> std::string {
        std::vector<std::string> keys(cmd.begin() + 1, cmd.end());
        int deleted = store.del(keys);
        return RespEncoder::integer(deleted);
    });

    // EXISTS key [key...]
    register_command("EXISTS", 2, -1, false, [](const Command& cmd, Store& store) -> std::string {
        int count = 0;
        for (size_t i = 1; i < cmd.size(); ++i) {
            if (store.exists(cmd[i])) count++;
        }
        return RespEncoder::integer(count);
    });

    // EXPIRE key seconds
    register_command("EXPIRE", 3, 3, true, [](const Command& cmd, Store& store) -> std::string {
        long sec = std::strtol(cmd[2].c_str(), nullptr, 10);
        bool ok = store.expire(cmd[1], sec);
        return RespEncoder::integer(ok ? 1 : 0);
    });

    // PEXPIRE key ms
    register_command("PEXPIRE", 3, 3, true, [](const Command& cmd, Store& store) -> std::string {
        long ms = std::strtol(cmd[2].c_str(), nullptr, 10);
        bool ok = store.pexpire(cmd[1], ms);
        return RespEncoder::integer(ok ? 1 : 0);
    });

    // TTL key
    register_command("TTL", 2, 2, false, [](const Command& cmd, Store& store) -> std::string {
        int64_t ms = store.ttl_ms(cmd[1]);
        if (ms < 0) return RespEncoder::integer(ms);
        return RespEncoder::integer(ms / 1000);
    });

    // PTTL key
    register_command("PTTL", 2, 2, false, [](const Command& cmd, Store& store) -> std::string {
        int64_t ms = store.ttl_ms(cmd[1]);
        return RespEncoder::integer(ms);
    });

    // PERSIST key
    register_command("PERSIST", 2, 2, true, [](const Command& cmd, Store& store) -> std::string {
        bool ok = store.persist(cmd[1]);
        return RespEncoder::integer(ok ? 1 : 0);
    });

    // INCR key
    register_command("INCR", 2, 2, true, [](const Command& cmd, Store& store) -> std::string {
        const std::string& key = cmd[1];
        Value* v = store.get(key);
        int64_t num = 0;
        if (v) {
            char* endptr = nullptr;
            num = std::strtoll(v->str_val.c_str(), &endptr, 10);
            if (*endptr != '\0') {
                return RespEncoder::error("value is not an integer or out of range");
            }
        }
        num++;
        store.set(key, Value(num));
        return RespEncoder::integer(num);
    });

    // DECR key
    register_command("DECR", 2, 2, true, [](const Command& cmd, Store& store) -> std::string {
        const std::string& key = cmd[1];
        Value* v = store.get(key);
        int64_t num = 0;
        if (v) {
            char* endptr = nullptr;
            num = std::strtoll(v->str_val.c_str(), &endptr, 10);
            if (*endptr != '\0') {
                return RespEncoder::error("value is not an integer or out of range");
            }
        }
        num--;
        store.set(key, Value(num));
        return RespEncoder::integer(num);
    });

    // INCRBY key increment
    register_command("INCRBY", 3, 3, true, [](const Command& cmd, Store& store) -> std::string {
        char* endptr = nullptr;
        int64_t step = std::strtoll(cmd[2].c_str(), &endptr, 10);
        if (*endptr != '\0') {
            return RespEncoder::error("value is not an integer or out of range");
        }
        const std::string& key = cmd[1];
        Value* v = store.get(key);
        int64_t num = 0;
        if (v) {
            num = std::strtoll(v->str_val.c_str(), &endptr, 10);
            if (*endptr != '\0') {
                return RespEncoder::error("value is not an integer or out of range");
            }
        }
        num += step;
        store.set(key, Value(num));
        return RespEncoder::integer(num);
    });

    // MSET key value [key value...]
    register_command("MSET", 3, -1, true, [](const Command& cmd, Store& store) -> std::string {
        if ((cmd.size() - 1) % 2 != 0) {
            return RespEncoder::error("wrong number of arguments for 'mset' command");
        }
        for (size_t i = 1; i < cmd.size(); i += 2) {
            store.set(cmd[i], Value(cmd[i + 1]));
        }
        return RespEncoder::ok();
    });

    // MGET key [key...]
    register_command("MGET", 2, -1, false, [](const Command& cmd, Store& store) -> std::string {
        std::string res = "*" + std::to_string(cmd.size() - 1) + "\r\n";
        for (size_t i = 1; i < cmd.size(); ++i) {
            Value* v = store.get(cmd[i]);
            if (!v || (v->type != ValueType::STRING && v->type != ValueType::INTEGER)) {
                res += RespEncoder::null_bulk();
            } else {
                res += RespEncoder::bulk_string(v->str_val);
            }
        }
        return res;
    });

    // LPUSH key val [val...]
    register_command("LPUSH", 3, -1, true, [](const Command& cmd, Store& store) -> std::string {
        int64_t len = 0;
        for (size_t i = 2; i < cmd.size(); ++i) {
            len = store.lpush(cmd[1], cmd[i]);
            if (len == -1) return RespEncoder::wrong_type();
        }
        return RespEncoder::integer(len);
    });

    // RPUSH key val [val...]
    register_command("RPUSH", 3, -1, true, [](const Command& cmd, Store& store) -> std::string {
        int64_t len = 0;
        for (size_t i = 2; i < cmd.size(); ++i) {
            len = store.rpush(cmd[1], cmd[i]);
            if (len == -1) return RespEncoder::wrong_type();
        }
        return RespEncoder::integer(len);
    });

    // LPOP key
    register_command("LPOP", 2, 2, true, [](const Command& cmd, Store& store) -> std::string {
        auto item = store.lpop(cmd[1]);
        if (!item) return RespEncoder::null_bulk();
        return RespEncoder::bulk_string(*item);
    });

    // RPOP key
    register_command("RPOP", 2, 2, true, [](const Command& cmd, Store& store) -> std::string {
        auto item = store.rpop(cmd[1]);
        if (!item) return RespEncoder::null_bulk();
        return RespEncoder::bulk_string(*item);
    });

    // LRANGE key start stop
    register_command("LRANGE", 4, 4, false, [](const Command& cmd, Store& store) -> std::string {
        long start = std::strtol(cmd[2].c_str(), nullptr, 10);
        long stop = std::strtol(cmd[3].c_str(), nullptr, 10);
        auto items = store.lrange(cmd[1], start, stop);
        return RespEncoder::array(items);
    });

    // LLEN key
    register_command("LLEN", 2, 2, false, [](const Command& cmd, Store& store) -> std::string {
        int64_t len = store.llen(cmd[1]);
        if (len == -1) return RespEncoder::wrong_type();
        return RespEncoder::integer(len);
    });

    // KEYS pattern
    register_command("KEYS", 2, 2, false, [](const Command& cmd, Store& store) -> std::string {
        auto keys = store.find_keys(cmd[1]);
        return RespEncoder::array(keys);
    });

    // INFO
    register_command("INFO", 1, 2, false, [](const Command&, Store& store) -> std::string {
        std::string info;
        info += "# Server\r\n";
        info += "miniredis_version:1.0.0\r\n";
        info += "os:POSIX\r\n";
        info += "# Keyspace\r\n";
        info += "db0:keys=" + std::to_string(store.size()) + ",expires=0,avg_ttl=0\r\n";
        return RespEncoder::bulk_string(info);
    });

    // COMMAND (handshake from redis-cli)
    register_command("COMMAND", 1, -1, false, [](const Command&, Store&) -> std::string {
        return "*0\r\n";
    });
}

} // namespace miniredis
