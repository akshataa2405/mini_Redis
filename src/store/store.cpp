#include "store.h"
#include <algorithm>

namespace miniredis {

// Helper glob pattern matcher (* and ?)
static bool match_glob(std::string_view pattern, std::string_view str) {
    size_t p = 0, s = 0;
    size_t star_p = std::string_view::npos;
    size_t match_s = 0;

    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            p++;
            s++;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p;
            match_s = s;
            p++;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            match_s++;
            s = match_s;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        p++;
    }

    return p == pattern.size();
}

bool Store::is_expired(const Value& val, int64_t current_time_ms) const {
    return val.expire_at_ms > 0 && val.expire_at_ms <= current_time_ms;
}

void Store::purge_if_expired(std::string_view key) {
    Value* v = db_.find(key);
    if (v && is_expired(*v, now_ms())) {
        db_.erase(key);
    }
}

SetResult Store::set(const std::string& key, Value val, bool nx, bool xx) {
    purge_if_expired(key);
    bool key_exists = (db_.find(key) != nullptr);

    if (nx && key_exists) {
        return SetResult::CONDITION_FAILED;
    }
    if (xx && !key_exists) {
        return SetResult::CONDITION_FAILED;
    }

    if (val.expire_at_ms > 0) {
        expiry_heap_.push(key, val.expire_at_ms);
    }

    db_.insert(key, std::move(val));
    return SetResult::OK;
}

Value* Store::get(std::string_view key) {
    purge_if_expired(key);
    return db_.find(key);
}

int Store::del(const std::vector<std::string>& keys) {
    int count = 0;
    for (const auto& k : keys) {
        purge_if_expired(k);
        if (db_.erase(k)) {
            count++;
        }
    }
    return count;
}

bool Store::exists(std::string_view key) {
    purge_if_expired(key);
    return db_.find(key) != nullptr;
}

bool Store::expire_at(std::string_view key, int64_t expire_at_ms) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v) return false;

    v->expire_at_ms = expire_at_ms;
    expiry_heap_.push(std::string(key), expire_at_ms);
    return true;
}

bool Store::expire(std::string_view key, int64_t seconds) {
    return expire_at(key, now_ms() + seconds * 1000);
}

bool Store::pexpire(std::string_view key, int64_t ms) {
    return expire_at(key, now_ms() + ms);
}

int64_t Store::ttl_ms(std::string_view key) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v) return -2; // Key does not exist
    if (v->expire_at_ms <= 0) return -1; // No timeout

    int64_t remaining = v->expire_at_ms - now_ms();
    return remaining > 0 ? remaining : -2;
}

bool Store::persist(std::string_view key) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v || v->expire_at_ms <= 0) return false;
    v->expire_at_ms = 0;
    return true;
}

int64_t Store::lpush(const std::string& key, const std::string& val) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v) {
        Value new_val;
        new_val.type = ValueType::LIST;
        new_val.list_val.push_front(val);
        db_.insert(key, std::move(new_val));
        return 1;
    }
    if (v->type != ValueType::LIST) {
        return -1; // Wrong type
    }
    v->list_val.push_front(val);
    return static_cast<int64_t>(v->list_val.size());
}

int64_t Store::rpush(const std::string& key, const std::string& val) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v) {
        Value new_val;
        new_val.type = ValueType::LIST;
        new_val.list_val.push_back(val);
        db_.insert(key, std::move(new_val));
        return 1;
    }
    if (v->type != ValueType::LIST) {
        return -1; // Wrong type
    }
    v->list_val.push_back(val);
    return static_cast<int64_t>(v->list_val.size());
}

std::optional<std::string> Store::lpop(const std::string& key) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v || v->type != ValueType::LIST || v->list_val.empty()) {
        return std::nullopt;
    }
    std::string item = std::move(v->list_val.front());
    v->list_val.pop_front();
    if (v->list_val.empty()) {
        db_.erase(key);
    }
    return item;
}

std::optional<std::string> Store::rpop(const std::string& key) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v || v->type != ValueType::LIST || v->list_val.empty()) {
        return std::nullopt;
    }
    std::string item = std::move(v->list_val.back());
    v->list_val.pop_back();
    if (v->list_val.empty()) {
        db_.erase(key);
    }
    return item;
}

std::vector<std::string> Store::lrange(const std::string& key, int64_t start, int64_t stop) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v || v->type != ValueType::LIST) {
        return {};
    }

    int64_t len = static_cast<int64_t>(v->list_val.size());
    if (len == 0) return {};

    // Adjust negative indices
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;

    if (start < 0) start = 0;
    if (start >= len || start > stop) return {};
    if (stop >= len) stop = len - 1;

    std::vector<std::string> result;
    result.reserve(stop - start + 1);
    for (int64_t i = start; i <= stop; ++i) {
        result.push_back(v->list_val[i]);
    }
    return result;
}

int64_t Store::llen(const std::string& key) {
    purge_if_expired(key);
    Value* v = db_.find(key);
    if (!v) return 0;
    if (v->type != ValueType::LIST) return -1;
    return static_cast<int64_t>(v->list_val.size());
}

std::vector<std::string> Store::find_keys(std::string_view pattern) {
    int64_t current_time = now_ms();
    std::vector<std::string> results;

    db_.for_each([&](const std::string& k, const Value& v) {
        if (!is_expired(v, current_time)) {
            if (pattern == "*" || match_glob(pattern, k)) {
                results.push_back(k);
            }
        }
    });

    return results;
}

size_t Store::size() {
    return find_keys("*").size();
}

size_t Store::active_expire(int64_t now_ms, size_t max_keys) {
    auto expired_candidates = expiry_heap_.pop_expired(now_ms, max_keys);
    size_t purged = 0;

    for (const auto& item : expired_candidates) {
        Value* v = db_.find(item.key);
        // Ensure it's still expired and has not been refreshed
        if (v && v->expire_at_ms > 0 && v->expire_at_ms <= now_ms) {
            db_.erase(item.key);
            purged++;
        }
    }
    return purged;
}

void Store::for_each(const std::function<void(const std::string&, const Value&)>& fn) {
    int64_t current_time = now_ms();
    db_.for_each([&](const std::string& k, const Value& v) {
        if (!is_expired(v, current_time)) {
            fn(k, v);
        }
    });
}

} // namespace miniredis
