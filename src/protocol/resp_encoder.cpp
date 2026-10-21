#include "resp_encoder.h"

namespace miniredis {

std::string RespEncoder::simple_string(std::string_view s) {
    std::string res;
    res.reserve(s.size() + 3);
    res += '+';
    res += s;
    res += "\r\n";
    return res;
}

std::string RespEncoder::ok() {
    return "+OK\r\n";
}

std::string RespEncoder::pong() {
    return "+PONG\r\n";
}

std::string RespEncoder::error(std::string_view msg) {
    std::string res;
    res.reserve(msg.size() + 3);
    res += '-';
    res += msg;
    res += "\r\n";
    return res;
}

std::string RespEncoder::wrong_type() {
    return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
}

std::string RespEncoder::syntax_error() {
    return "-ERR syntax error\r\n";
}

std::string RespEncoder::integer(int64_t val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string RespEncoder::bulk_string(std::string_view s) {
    std::string len_str = std::to_string(s.size());
    std::string res;
    res.reserve(1 + len_str.size() + 2 + s.size() + 2);
    res += '$';
    res += len_str;
    res += "\r\n";
    res += s;
    res += "\r\n";
    return res;
}

std::string RespEncoder::null_bulk() {
    return "$-1\r\n";
}

std::string RespEncoder::null_array() {
    return "*-1\r\n";
}

std::string RespEncoder::array(const std::vector<std::string>& bulk_elements) {
    std::string res;
    res += '*' + std::to_string(bulk_elements.size()) + "\r\n";
    for (const auto& elem : bulk_elements) {
        res += bulk_string(elem);
    }
    return res;
}

} // namespace miniredis
