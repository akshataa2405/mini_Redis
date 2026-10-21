#include "logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace miniredis {

LogLevel Logger::current_level_ = LogLevel::INFO;
std::mutex Logger::log_mutex_;

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

LogLevel Logger::get_level() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return current_level_;
}

void Logger::log(LogLevel level, std::string_view component, std::string_view msg) {
    if (level < current_level_) {
        return;
    }

    const char* level_str = "INFO";
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO:  level_str = "INFO";  break;
        case LogLevel::WARN:  level_str = "WARN";  break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::tm bt{};
#if defined(_WIN32)
    localtime_s(&bt, &in_time_t);
#else
    localtime_r(&in_time_t, &bt);
#endif

    std::lock_guard<std::mutex> lock(log_mutex_);
    std::cerr << '[' << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
              << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] ["
              << level_str << "] ["
              << component << "] "
              << msg << '\n';
}

} // namespace miniredis
