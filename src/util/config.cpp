#include "config.h"
#include <iostream>
#include <sstream>

namespace miniredis {

Config parse_config(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--bind" && i + 1 < argc) {
            cfg.bind_addr = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            cfg.timeout_s = std::stoi(argv[++i]);
        } else if (arg == "--maxmemory" && i + 1 < argc) {
            cfg.maxmemory = std::stoull(argv[++i]);
        } else if (arg == "--aof" && i + 1 < argc) {
            std::string val = argv[++i];
            cfg.aof_enabled = (val == "yes" || val == "1" || val == "true");
        } else if (arg == "--aof-fsync" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "always") cfg.aof_fsync = AofFsync::ALWAYS;
            else if (val == "everysec") cfg.aof_fsync = AofFsync::EVERYSEC;
            else if (val == "no") cfg.aof_fsync = AofFsync::NO;
        } else if (arg == "--requirepass" && i + 1 < argc) {
            cfg.requirepass = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: miniredisd [options]\n"
                      << "  --port <port>         Listen port (default: 6380)\n"
                      << "  --bind <addr>         Bind IP (default: 127.0.0.1)\n"
                      << "  --timeout <sec>       Client idle timeout in seconds (default: 300)\n"
                      << "  --maxmemory <bytes>   Max memory in bytes (0 for unlimited)\n"
                      << "  --aof <yes|no>        Enable AOF (default: no)\n"
                      << "  --aof-fsync <policy>  always | everysec | no (default: everysec)\n"
                      << "  --requirepass <pass>  Require client AUTH\n";
            exit(0);
        }
    }
    return cfg;
}

} // namespace miniredis
