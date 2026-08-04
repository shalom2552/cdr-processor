#pragma once
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>

namespace cdrp {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    None
};

class Logger {
public:
    static Logger& instance();

    /* Log a message at the specified level, tagged with its component */
    void log(LogLevel level, std::string_view component, const std::string& message);

    /* Set the logging level to one of the LogLevel enum values */
    void setLevel(LogLevel level);

    /* Returns the active log level */
    LogLevel level() const { return m_level; }

    /* Maps a config level name to a log level, defaults to Info */
    static LogLevel levelFromName(std::string_view name);

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::mutex m_mutex;
    std::atomic<LogLevel> m_level{LogLevel::Info};
};

inline void logInfo(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Info, component, message);
}

inline void logWarn(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Warning, component, message);
}

inline void logError(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Error, component, message);
}

inline void logDebug(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Debug, component, message);
}

} // namespace cdrp

