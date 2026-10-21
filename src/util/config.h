#pragma once

#include <cstdint>
#include <string>

namespace miniredis {

enum class AofFsync {
    ALWAYS,
    EVERYSEC,
    NO
};

struct Config {
    uint16_t port        = 6380;
    std::string bind_addr = "127.0.0.1";
    int      timeout_s   = 300;       // Idle connection timeout in seconds
    size_t   maxmemory   = 0;         // 0 = unlimited bytes
    std::string aof_path = "appendonly.aof";
    std::string rdb_path = "dump.rdb";
    AofFsync aof_fsync   = AofFsync::EVERYSEC;
    std::string requirepass = "";
    bool     aof_enabled = false;
};

Config parse_config(int argc, char** argv);

} // namespace miniredis
