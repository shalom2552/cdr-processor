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

    /**
     * Writes one line to stderr, dropped if below the active level.
     *
     * @param level: the severity of the message
     * @param component: the name of the emitting component
     * @param message: the text to log
     */
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

/**
 * Logs a message at Info level.
 *
 * @param component: the name of the emitting component
 * @param message: the text to log
 */
inline void logInfo(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Info, component, message);
}

/**
 * Logs a message at Warning level.
 *
 * @param component: the name of the emitting component
 * @param message: the text to log
 */
inline void logWarn(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Warning, component, message);
}

/**
 * Logs a message at Error level.
 *
 * @param component: the name of the emitting component
 * @param message: the text to log
 */
inline void logError(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Error, component, message);
}

/**
 * Logs a message at Debug level.
 *
 * @param component: the name of the emitting component
 * @param message: the text to log
 */
inline void logDebug(std::string_view component, const std::string& message) {
    Logger::instance().log(cdrp::LogLevel::Debug, component, message);
}

} // namespace cdrp

