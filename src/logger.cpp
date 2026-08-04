#include "logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace cdrp {

/* Helper function to get the name of a log level */
static std::string levelName(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::None:
            return "";
    }
    return "";
}

/* Helper function to get the ANSI color of a log level */
static const char* levelColor(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug:
            return "\033[90m"; // gray
        case LogLevel::Info:
            return "\033[32m"; // green
        case LogLevel::Warning:
            return "\033[33m"; // yellow
        case LogLevel::Error:
            return "\033[31m"; // red
        case LogLevel::None:
            return "";
    }
    return "";
}

LogLevel Logger::levelFromName(std::string_view name)
{
    if (name == "debug") {
        return LogLevel::Debug;
    } else if (name == "info") {
        return LogLevel::Info;
    } else if (name == "warn") {
        return LogLevel::Warning;
    } else if (name == "error") {
        return LogLevel::Error;
    } else if (name == "none") {
        return LogLevel::None;
    }
    return LogLevel::Info;
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, std::string_view component, const std::string& message)
{
    if (level == LogLevel::None || level < m_level) {
        return;
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);

    // One lock around the whole line, otherwise concurrent logs interleave
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cerr << "\033[90m" << std::put_time(&tm, "%m-%d %T") << "\033[0m "
              << levelColor(level) << '[' << levelName(level) << "]\033[0m "
              << '[' << component << "] -> " << message << '\n';
}

void Logger::setLevel(LogLevel level)
{
    m_level = level;
}

} // namespace cdrp

