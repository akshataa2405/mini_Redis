#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace miniredis {

class RespEncoder {
public:
    static std::string simple_string(std::string_view s);
    static std::string ok();
    static std::string pong();
    static std::string error(std::string_view msg);
    static std::string wrong_type();
    static std::string syntax_error();
    static std::string integer(int64_t val);
    static std::string bulk_string(std::string_view s);
    static std::string null_bulk();
    static std::string null_array();
    static std::string array(const std::vector<std::string>& bulk_elements);
};

} // namespace miniredis
