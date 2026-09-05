#include "diagnostics/Logger.hpp"

#include <chrono>
#include <format>
#include <iostream>

namespace strategy {

std::string_view logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::trace: return "TRACE";
    case LogLevel::debug: return "DEBUG";
    case LogLevel::info: return "INFO";
    case LogLevel::warning: return "WARN";
    case LogLevel::error: return "ERROR";
    case LogLevel::critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

Logger::Logger(const std::filesystem::path& path, LogLevel minimumLevel)
    : path_(path), minimumLevel_(minimumLevel) {
    if (!path_.parent_path().empty())
        std::filesystem::create_directories(path_.parent_path());
    file_.open(path_, std::ios::app);
    info("lifecycle", "Logger initialized");
}

void Logger::log(LogLevel level,
                 std::string_view category,
                 std::string_view message,
                 const std::source_location& location) {
    if (level < minimumLevel_)
        return;
    const auto now = std::chrono::system_clock::now();
    LogEntry entry{level,
                   std::format("{:%Y-%m-%d %H:%M:%S}", now),
                   std::string(category),
                   std::string(message),
                   std::format("{}:{}", location.file_name(), location.line())};
    const std::string line = std::format("{} [{}] [{}] {} ({})",
                                         entry.timestamp,
                                         logLevelName(entry.level),
                                         entry.category,
                                         entry.message,
                                         entry.source);
    std::scoped_lock lock(mutex_);
    (level >= LogLevel::warning ? std::cerr : std::clog) << line << '\n';
    if (file_)
        file_ << line << '\n';
    recent_.push_back(std::move(entry));
    if (recent_.size() > recentCapacity)
        recent_.pop_front();
}

#define STRATEGY_LOG_METHOD(name, value)                                                           \
    void Logger::name(std::string_view category,                                                   \
                      std::string_view message,                                                    \
                      const std::source_location& location) {                                      \
        log(LogLevel::value, category, message, location);                                         \
    }
STRATEGY_LOG_METHOD(trace, trace)
STRATEGY_LOG_METHOD(debug, debug)
STRATEGY_LOG_METHOD(info, info)
STRATEGY_LOG_METHOD(warning, warning)
STRATEGY_LOG_METHOD(error, error)
#undef STRATEGY_LOG_METHOD

std::vector<LogEntry> Logger::recentEntries() const {
    std::scoped_lock lock(mutex_);
    return {recent_.begin(), recent_.end()};
}

void Logger::flush() {
    std::scoped_lock lock(mutex_);
    if (file_)
        file_.flush();
}

} // namespace strategy
