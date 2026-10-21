#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace miniredis {

struct ExpiryNode {
    int64_t expire_at_ms = 0;
    std::string key;

    bool operator>(const ExpiryNode& other) const {
        return expire_at_ms > other.expire_at_ms;
    }
};

class ExpiryHeap {
public:
    void push(std::string key, int64_t expire_at_ms) {
        heap_.push_back({expire_at_ms, std::move(key)});
        sift_up(heap_.size() - 1);
    }

    bool empty() const {
        return heap_.empty();
    }

    size_t size() const {
        return heap_.size();
    }

    // Peek the smallest expiration timestamp
    int64_t next_expire_ms() const {
        if (heap_.empty()) return -1;
        return heap_.front().expire_at_ms;
    }

    // Pop root element
    ExpiryNode pop_min() {
        if (heap_.empty()) return {};
        ExpiryNode min_node = std::move(heap_.front());
        heap_.front() = std::move(heap_.back());
        heap_.pop_back();
        if (!heap_.empty()) {
            sift_down(0);
        }
        return min_node;
    }

    // Collect keys expired up to now_ms, up to max_count (for time-boxed active expiry)
    std::vector<ExpiryNode> pop_expired(int64_t now_ms, size_t max_count = 64) {
        std::vector<ExpiryNode> expired;
        while (!heap_.empty() && heap_.front().expire_at_ms <= now_ms && expired.size() < max_count) {
            expired.push_back(pop_min());
        }
        return expired;
    }

    void clear() {
        heap_.clear();
    }

private:
    std::vector<ExpiryNode> heap_;

    void sift_up(size_t idx) {
        while (idx > 0) {
            size_t parent = (idx - 1) / 2;
            if (heap_[idx].expire_at_ms < heap_[parent].expire_at_ms) {
                std::swap(heap_[idx], heap_[parent]);
                idx = parent;
            } else {
                break;
            }
        }
    }

    void sift_down(size_t idx) {
        size_t n = heap_.size();
        while (true) {
            size_t left = 2 * idx + 1;
            size_t right = 2 * idx + 2;
            size_t smallest = idx;

            if (left < n && heap_[left].expire_at_ms < heap_[smallest].expire_at_ms) {
                smallest = left;
            }
            if (right < n && heap_[right].expire_at_ms < heap_[smallest].expire_at_ms) {
                smallest = right;
            }

            if (smallest != idx) {
                std::swap(heap_[idx], heap_[smallest]);
                idx = smallest;
            } else {
                break;
            }
        }
    }
};

} // namespace miniredis
