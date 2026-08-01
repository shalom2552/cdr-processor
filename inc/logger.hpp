#pragma once
#include <string>
#include <atomic>
#include <mutex>

namespace cdrp {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& instance();

    void log(LogLevel level, const std::string& message);

    void setLevel(LogLevel level);

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

