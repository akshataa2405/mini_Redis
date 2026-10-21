#pragma once

#include <chrono>
#include <cstdint>

namespace miniredis {

// Wall-clock milliseconds since UNIX epoch (used for key expiry, logging)
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Monotonic milliseconds (used for internal timeouts, tick calculations, latency measurements)
inline int64_t mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace miniredis
