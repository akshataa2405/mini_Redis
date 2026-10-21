#pragma once

#include "hash_table.h"
#include "value.h"
#include "expiry_heap.h"
#include "../util/time_util.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>

namespace miniredis {

enum class SetResult {
    OK,
    CONDITION_FAILED, // NX or XX condition not met
    ERROR
};

class Store {
public:
    Store() = default;

    // String operations
    SetResult set(const std::string& key, Value val, bool nx = false, bool xx = false);
    Value* get(std::string_view key); // Performs lazy expiry check
    int del(const std::vector<std::string>& keys);
    bool exists(std::string_view key);

    // TTL operations
    bool expire_at(std::string_view key, int64_t expire_at_ms);
    bool expire(std::string_view key, int64_t seconds);
    bool pexpire(std::string_view key, int64_t ms);
    int64_t ttl_ms(std::string_view key); // -2 missing, -1 no expiry, >= 0 ms left
    bool persist(std::string_view key);

    // List operations
    int64_t lpush(const std::string& key, const std::string& val);
    int64_t rpush(const std::string& key, const std::string& val);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int64_t start, int64_t stop);
    int64_t llen(const std::string& key);

    // Key queries
    std::vector<std::string> find_keys(std::string_view pattern);
    size_t size();

    // Active expiration step (called from server loop tick)
    size_t active_expire(int64_t now_ms, size_t max_keys = 64);

    // Direct iteration (for AOF rewrite / RDB snapshot / inspection)
    void for_each(const std::function<void(const std::string&, const Value&)>& fn);

private:
    HashTable<Value> db_;
    ExpiryHeap expiry_heap_;

    bool is_expired(const Value& val, int64_t now_ms) const;
    void purge_if_expired(std::string_view key);
};

} // namespace miniredis
