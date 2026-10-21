#pragma once

#include "connection.h"
#include "../util/config.h"
#include "../store/store.h"
#include "../commands/command_table.h"
#include "../protocol/resp_parser.h"
#include <unordered_map>
#include <vector>
#include <poll.h>

namespace miniredis {

class Server {
public:
    explicit Server(Config cfg);
    ~Server();

    bool init();
    void run();
    void stop();

    Store& get_store() { return store_; }

private:
    Config cfg_;
    int listen_fd_ = -1;
    bool running_ = false;

    Store store_;
    CommandTable command_table_;
    RespParser parser_;

    std::unordered_map<int, Connection> connections_;
    std::vector<struct pollfd> poll_fds_;

    void handle_accept();
    void handle_client_read(Connection& conn);
    void handle_client_write(Connection& conn);
    void close_connection(int fd);
    void tick();
};

} // namespace miniredis
