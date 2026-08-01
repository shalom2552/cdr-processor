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

    /* Log a message at the specified level */
    void log(LogLevel level, const std::string& message);

    /* Set the logging level to one of the LogLevel enum values */
    void setLevel(LogLevel level);

    /* Maps a config level name to a log level, defaults to Info */
    static LogLevel levelFromName(std::string_view name);

private:
    Logger();
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::mutex m_mutex;
    std::atomic<LogLevel> m_level{LogLevel::Info};
};

inline void logInfo(const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Info, message);
}

inline void logWarn(const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Warning, message);
}

inline void logError(const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Error, message);
}

inline void logDebug(const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Debug, message);
}

} // namespace cdrp

