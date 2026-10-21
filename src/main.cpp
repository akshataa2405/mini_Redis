#include "util/config.h"
#include "util/logger.h"
#include "net/server.h"
#include <csignal>
#include <iostream>

static miniredis::Server* g_server = nullptr;

static void handle_signal(int sig) {
    if (g_server) {
        LOG_INFO("main", "Received signal " + std::to_string(sig) + ", shutting down...");
        g_server->stop();
    }
}

int main(int argc, char** argv) {
    miniredis::Config cfg = miniredis::parse_config(argc, argv);

    std::cout << R"(
  __  __ _       _ _____          _ _     
 |  \/  (_)     (_)  __ \        | (_)    
 | \  / |_ _ __  _| |__) |___  __| |_ ___ 
 | |\/| | | '_ \| |  _  // _ \/ _` | / __|
 | |  | | | | | | | | \ \  __/ (_| | \__ \
 |_|  |_|_|_| |_|_|_|  \_\___|\__,_|_|___/
    )" << '\n';

    LOG_INFO("main", "Starting MiniRedis on port " + std::to_string(cfg.port));

    miniredis::Server server(cfg);
    g_server = &server;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (!server.init()) {
        LOG_ERROR("main", "Server initialization failed!");
        return 1;
    }

    server.run();
    LOG_INFO("main", "Server shutdown complete. Goodbye!");
    return 0;
}
