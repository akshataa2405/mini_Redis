#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>

namespace miniredis {

enum class ValueType {
    STRING = 0,
    INTEGER = 1,
    LIST = 2
};

struct Value {
    ValueType type = ValueType::STRING;
    std::string str_val;
    int64_t int_val = 0;
    std::deque<std::string> list_val;
    int64_t expire_at_ms = 0; // 0 means no expiration

    Value() = default;
    explicit Value(std::string s, int64_t expire_at = 0)
        : type(ValueType::STRING), str_val(std::move(s)), expire_at_ms(expire_at) {}
    explicit Value(int64_t i, int64_t expire_at = 0)
        : type(ValueType::INTEGER), int_val(i), expire_at_ms(expire_at) {
        str_val = std::to_string(i);
    }
};

} // namespace miniredis
