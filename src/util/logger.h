#pragma once

#include <string_view>
#include <mutex>

namespace miniredis {

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

class Logger {
public:
    static void set_level(LogLevel level);
    static LogLevel get_level();
    static void log(LogLevel level, std::string_view component, std::string_view msg);

private:
    static LogLevel current_level_;
    static std::mutex log_mutex_;
};

#define LOG_DEBUG(comp, msg) ::miniredis::Logger::log(::miniredis::LogLevel::DEBUG, comp, msg)
#define LOG_INFO(comp, msg)  ::miniredis::Logger::log(::miniredis::LogLevel::INFO,  comp, msg)
#define LOG_WARN(comp, msg)  ::miniredis::Logger::log(::miniredis::LogLevel::WARN,  comp, msg)
#define LOG_ERROR(comp, msg) ::miniredis::Logger::log(::miniredis::LogLevel::ERROR, comp, msg)

} // namespace miniredis
