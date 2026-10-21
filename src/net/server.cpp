#include "server.h"
#include "../util/logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <cerrno>

namespace miniredis {

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

Server::Server(Config cfg) : cfg_(std::move(cfg)) {}

Server::~Server() {
    stop();
}

bool Server::init() {
    signal(SIGPIPE, SIG_IGN);

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR("server", "Failed to create listen socket: " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (!set_nonblocking(listen_fd_)) {
        LOG_ERROR("server", "Failed to set listen socket non-blocking");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg_.port);
    if (inet_pton(AF_INET, cfg_.bind_addr.c_str(), &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("server", "Failed to bind to " + cfg_.bind_addr + ":" + std::to_string(cfg_.port) + " : " + std::string(strerror(errno)));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 512) < 0) {
        LOG_ERROR("server", "Failed to listen on socket");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    LOG_INFO("server", "MiniRedis server initialized and listening on " + cfg_.bind_addr + ":" + std::to_string(cfg_.port));
    return true;
}

void Server::handle_accept() {
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // All pending incoming connections accepted
            }
            LOG_WARN("server", "accept() failed: " + std::string(strerror(errno)));
            break;
        }

        if (!set_nonblocking(client_fd)) {
            close(client_fd);
            continue;
        }

        Connection conn;
        conn.fd = client_fd;
        conn.last_active_ms = mono_ms();
        connections_[client_fd] = std::move(conn);

        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        LOG_INFO("server", "Accepted client connection from " + std::string(ip_str) + ":" + std::to_string(ntohs(client_addr.sin_port)) + " (fd=" + std::to_string(client_fd) + ")");
    }
}

void Server::handle_client_read(Connection& conn) {
    char buf[16384];
    while (true) {
        ssize_t bytes_read = recv(conn.fd, buf, sizeof(buf), 0);
        if (bytes_read > 0) {
            conn.in_buf.append(buf, bytes_read);
            conn.last_active_ms = mono_ms();
        } else if (bytes_read == 0) {
            // Client closed connection
            conn.is_closing = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more data to read right now
            }
            conn.is_closing = true;
            break;
        }
    }

    if (!conn.in_buf.empty()) {
        std::vector<Command> cmds;
        ParseStatus status = parser_.parse(conn.in_buf, cmds);

        if (status == ParseStatus::ERROR) {
            conn.out_buf += "-ERR Protocol error: invalid input\r\n";
            conn.is_closing = true;
        } else {
            for (const auto& cmd : cmds) {
                std::string reply = command_table_.execute(cmd, store_);
                conn.out_buf += reply;
            }
        }
    }

    if (!conn.out_buf.empty()) {
        handle_client_write(conn);
    }
}

void Server::handle_client_write(Connection& conn) {
    while (!conn.out_buf.empty()) {
        ssize_t sent = send(conn.fd, conn.out_buf.data(), conn.out_buf.size(), 0);
        if (sent > 0) {
            conn.out_buf.erase(0, sent);
            conn.last_active_ms = mono_ms();
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Socket send buffer full, wait for next POLLOUT
            }
            conn.is_closing = true;
            break;
        }
    }
}

void Server::close_connection(int fd) {
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        LOG_INFO("server", "Closing client connection (fd=" + std::to_string(fd) + ")");
        close(fd);
        connections_.erase(it);
    }
}

void Server::tick() {
    int64_t current_mono = mono_ms();
    int64_t current_wall = now_ms();

    // 1. Active TTL expiry pass
    store_.active_expire(current_wall, 64);

    // 2. Idle client timeout pass
    if (cfg_.timeout_s > 0) {
        int64_t max_idle_ms = static_cast<int64_t>(cfg_.timeout_s) * 1000;
        for (auto& [fd, conn] : connections_) {
            if (!conn.is_closing && (current_mono - conn.last_active_ms > max_idle_ms)) {
                LOG_INFO("server", "Client fd=" + std::to_string(fd) + " timed out due to inactivity");
                conn.is_closing = true;
            }
        }
    }
}

void Server::run() {
    running_ = true;

    while (running_) {
        poll_fds_.clear();

        // 1. Listen socket
        struct pollfd listen_pfd{};
        listen_pfd.fd = listen_fd_;
        listen_pfd.events = POLLIN;
        poll_fds_.push_back(listen_pfd);

        // 2. Client sockets
        for (const auto& [fd, conn] : connections_) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            if (!conn.out_buf.empty()) {
                pfd.events |= POLLOUT;
            }
            poll_fds_.push_back(pfd);
        }

        // Wait up to 100 ms per tick
        int ret = poll(poll_fds_.data(), poll_fds_.size(), 100);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("server", "poll() error: " + std::string(strerror(errno)));
            break;
        }

        // Handle listen socket
        if (poll_fds_[0].revents & POLLIN) {
            handle_accept();
        }

        // Handle client sockets
        for (size_t i = 1; i < poll_fds_.size(); ++i) {
            int fd = poll_fds_[i].fd;
            short revents = poll_fds_[i].revents;

            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;
            Connection& conn = it->second;

            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                conn.is_closing = true;
            }

            if (!conn.is_closing && (revents & POLLIN)) {
                handle_client_read(conn);
            }

            if (!conn.is_closing && (revents & POLLOUT)) {
                handle_client_write(conn);
            }
        }

        // Run tick (active expiration + idle timeouts)
        tick();

        // Cleanup closed connections
        std::vector<int> to_remove;
        for (const auto& [fd, conn] : connections_) {
            if (conn.is_closing && conn.out_buf.empty()) {
                to_remove.push_back(fd);
            }
        }
        for (int fd : to_remove) {
            close_connection(fd);
        }
    }
}

void Server::stop() {
    running_ = false;
    for (auto& [fd, conn] : connections_) {
        close(fd);
    }
    connections_.clear();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

} // namespace miniredis
