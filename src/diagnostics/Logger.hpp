#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace strategy {

enum class LogLevel { trace, debug, info, warning, error, critical };

struct LogEntry {
    LogLevel level;
    std::string timestamp;
    std::string category;
    std::string message;
    std::string source;
};

class Logger final {
  public:
    explicit Logger(const std::filesystem::path& path = "gamedata/logs/strategy-game.log",
                    LogLevel minimumLevel = LogLevel::debug);

    void log(LogLevel level,
             std::string_view category,
             std::string_view message,
             const std::source_location& location = std::source_location::current());
    void trace(std::string_view category,
               std::string_view message,
               const std::source_location& location = std::source_location::current());
    void debug(std::string_view category,
               std::string_view message,
               const std::source_location& location = std::source_location::current());
    void info(std::string_view category,
              std::string_view message,
              const std::source_location& location = std::source_location::current());
    void warning(std::string_view category,
                 std::string_view message,
                 const std::source_location& location = std::source_location::current());
    void error(std::string_view category,
               std::string_view message,
               const std::source_location& location = std::source_location::current());

    [[nodiscard]] std::vector<LogEntry> recentEntries() const;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    void flush();

  private:
    static constexpr std::size_t recentCapacity = 256;
    std::filesystem::path path_;
    LogLevel minimumLevel_;
    mutable std::mutex mutex_;
    std::ofstream file_;
    std::deque<LogEntry> recent_;
};

[[nodiscard]] std::string_view logLevelName(LogLevel level);

} // namespace strategy
