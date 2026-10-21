#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <utility>

namespace miniredis {

// 64-bit FNV-1a hash function
inline uint64_t fnv1a_hash(std::string_view s) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
class HashTable {
public:
    struct Entry {
        std::string key;
        T val;
        Entry* next = nullptr;

        Entry(std::string k, T v) : key(std::move(k)), val(std::move(v)) {}
    };

    explicit HashTable(size_t initial_capacity = 16) {
        size_t cap = 16;
        while (cap < initial_capacity) {
            cap <<= 1;
        }
        tables_[0].init(cap);
    }

    ~HashTable() {
        clear();
    }

    // Non-copyable for clear memory ownership
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    HashTable(HashTable&& other) noexcept {
        move_from(std::move(other));
    }

    HashTable& operator=(HashTable&& other) noexcept {
        if (this != &other) {
            clear();
            move_from(std::move(other));
        }
        return *this;
    }

    size_t size() const {
        return tables_[0].count + tables_[1].count;
    }

    bool empty() const {
        return size() == 0;
    }

    bool is_rehashing() const {
        return rehash_idx_ != -1;
    }

    size_t bucket_count() const {
        return tables_[0].capacity + tables_[1].capacity;
    }

    T* find(std::string_view key) {
        if (is_rehashing()) {
            rehash_step(1);
        }

        uint64_t h = fnv1a_hash(key);
        // Check primary table
        if (tables_[0].capacity > 0) {
            size_t idx = h & (tables_[0].capacity - 1);
            Entry* curr = tables_[0].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    return &curr->val;
                }
                curr = curr->next;
            }
        }

        // If rehashing, check secondary table
        if (is_rehashing() && tables_[1].capacity > 0) {
            size_t idx = h & (tables_[1].capacity - 1);
            Entry* curr = tables_[1].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    return &curr->val;
                }
                curr = curr->next;
            }
        }

        return nullptr;
    }

    const T* find(std::string_view key) const {
        uint64_t h = fnv1a_hash(key);
        if (tables_[0].capacity > 0) {
            size_t idx = h & (tables_[0].capacity - 1);
            Entry* curr = tables_[0].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    return &curr->val;
                }
                curr = curr->next;
            }
        }

        if (is_rehashing() && tables_[1].capacity > 0) {
            size_t idx = h & (tables_[1].capacity - 1);
            Entry* curr = tables_[1].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    return &curr->val;
                }
                curr = curr->next;
            }
        }

        return nullptr;
    }

    // Insert or update. Returns true if newly inserted, false if updated.
    bool insert(const std::string& key, const T& val) {
        if (is_rehashing()) {
            rehash_step(8);
        } else if (tables_[0].count >= tables_[0].capacity) {
            start_rehash();
        }

        uint64_t h = fnv1a_hash(key);

        // If already exists in tables_[0], update in place
        if (tables_[0].capacity > 0) {
            size_t idx = h & (tables_[0].capacity - 1);
            Entry* curr = tables_[0].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    curr->val = val;
                    return false;
                }
                curr = curr->next;
            }
        }

        // If rehashing, also check tables_[1]
        if (is_rehashing() && tables_[1].capacity > 0) {
            size_t idx = h & (tables_[1].capacity - 1);
            Entry* curr = tables_[1].buckets[idx];
            while (curr) {
                if (curr->key == key) {
                    curr->val = val;
                    return false;
                }
                curr = curr->next;
            }
        }

        // New entry: if rehashing, insert into tables_[1], else tables_[0]
        Table& target = is_rehashing() ? tables_[1] : tables_[0];
        size_t idx = h & (target.capacity - 1);
        Entry* new_entry = new Entry(key, val);
        new_entry->next = target.buckets[idx];
        target.buckets[idx] = new_entry;
        target.count++;
        return true;
    }

    bool erase(std::string_view key) {
        if (is_rehashing()) {
            rehash_step(8);
        }

        uint64_t h = fnv1a_hash(key);

        // Try erasing from tables_[0]
        if (tables_[0].capacity > 0) {
            size_t idx = h & (tables_[0].capacity - 1);
            Entry* curr = tables_[0].buckets[idx];
            Entry* prev = nullptr;
            while (curr) {
                if (curr->key == key) {
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        tables_[0].buckets[idx] = curr->next;
                    }
                    delete curr;
                    tables_[0].count--;
                    return true;
                }
                prev = curr;
                curr = curr->next;
            }
        }

        // Try erasing from tables_[1] if rehashing
        if (is_rehashing() && tables_[1].capacity > 0) {
            size_t idx = h & (tables_[1].capacity - 1);
            Entry* curr = tables_[1].buckets[idx];
            Entry* prev = nullptr;
            while (curr) {
                if (curr->key == key) {
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        tables_[1].buckets[idx] = curr->next;
                    }
                    delete curr;
                    tables_[1].count--;
                    return true;
                }
                prev = curr;
                curr = curr->next;
            }
        }

        return false;
    }

    void for_each(const std::function<void(const std::string&, T&)>& fn) {
        for (int t = 0; t < 2; ++t) {
            for (size_t i = 0; i < tables_[t].capacity; ++i) {
                Entry* curr = tables_[t].buckets[i];
                while (curr) {
                    fn(curr->key, curr->val);
                    curr = curr->next;
                }
            }
        }
    }

    void for_each(const std::function<void(const std::string&, const T&)>& fn) const {
        for (int t = 0; t < 2; ++t) {
            for (size_t i = 0; i < tables_[t].capacity; ++i) {
                Entry* curr = tables_[t].buckets[i];
                while (curr) {
                    fn(curr->key, curr->val);
                    curr = curr->next;
                }
            }
        }
    }

    void clear() {
        for (int t = 0; t < 2; ++t) {
            for (size_t i = 0; i < tables_[t].capacity; ++i) {
                Entry* curr = tables_[t].buckets[i];
                while (curr) {
                    Entry* next = curr->next;
                    delete curr;
                    curr = next;
                }
                tables_[t].buckets[i] = nullptr;
            }
            tables_[t].count = 0;
            tables_[t].capacity = 0;
            tables_[t].buckets.clear();
        }
        rehash_idx_ = -1;
    }

private:
    struct Table {
        std::vector<Entry*> buckets;
        size_t capacity = 0;
        size_t count = 0;

        void init(size_t cap) {
            capacity = cap;
            count = 0;
            buckets.assign(cap, nullptr);
        }
    };

    Table tables_[2];
    int64_t rehash_idx_ = -1; // -1 when not rehashing

    void start_rehash() {
        size_t new_cap = tables_[0].capacity == 0 ? 16 : tables_[0].capacity * 2;
        tables_[1].init(new_cap);
        rehash_idx_ = 0;
    }

    void rehash_step(size_t step_count = 8) {
        if (!is_rehashing()) return;

        while (step_count > 0 && rehash_idx_ < static_cast<int64_t>(tables_[0].capacity)) {
            Entry* curr = tables_[0].buckets[rehash_idx_];
            while (curr) {
                Entry* next = curr->next;
                uint64_t h = fnv1a_hash(curr->key);
                size_t new_idx = h & (tables_[1].capacity - 1);

                curr->next = tables_[1].buckets[new_idx];
                tables_[1].buckets[new_idx] = curr;
                tables_[0].count--;
                tables_[1].count++;

                curr = next;
            }
            tables_[0].buckets[rehash_idx_] = nullptr;
            rehash_idx_++;
            step_count--;
        }

        // Check if rehashing finished
        if (rehash_idx_ >= static_cast<int64_t>(tables_[0].capacity)) {
            tables_[0] = std::move(tables_[1]);
            tables_[1].init(0);
            rehash_idx_ = -1;
        }
    }

    void move_from(HashTable&& other) noexcept {
        tables_[0] = std::move(other.tables_[0]);
        tables_[1] = std::move(other.tables_[1]);
        rehash_idx_ = other.rehash_idx_;
        other.rehash_idx_ = -1;
    }
};

} // namespace miniredis
