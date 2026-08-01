#include "logger.hpp"
#include "config.hpp"
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
    }
    return "";
}

Logger::Logger()
{
    // set the log level according to config
    if (cfg.log.level == "debug") {
        m_level = LogLevel::Debug;
    } else if (cfg.log.level == "info") {
        m_level = LogLevel::Info;
    } else if (cfg.log.level == "warn") {
        m_level = LogLevel::Warning;
    } else if (cfg.log.level == "error") {
        m_level = LogLevel::Error;
    }
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const std::string& message)
{
    if (level < m_level) {
        return;
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);

    // One lock around the whole line, otherwise concurrent logs interleave
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cerr << "\033[90m" << std::put_time(&tm, "%F %T") << "\033[0m "
              << levelColor(level) << '[' << levelName(level) << "]\033[0m "
              << message << '\n';
}

void Logger::setLevel(LogLevel level)
{
    m_level = level;
}

} // namespace cdrp

