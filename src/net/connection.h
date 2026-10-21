#pragma once

#include <string>
#include <cstdint>

namespace miniredis {

struct Connection {
    int fd = -1;
    std::string in_buf;
    std::string out_buf;
    int64_t last_active_ms = 0;
    bool is_closing = false;
};

} // namespace miniredis
