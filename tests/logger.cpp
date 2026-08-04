#include "doctest.h"
#include "config.hpp"
#include "logger.hpp"

#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

/* Redirects std::cerr into a buffer for the lifetime of the object */
class CerrCapture {
public:
    CerrCapture()
        : m_old(std::cerr.rdbuf(m_buf.rdbuf()))
    {
    }

    ~CerrCapture() { std::cerr.rdbuf(m_old); }

    std::string str() const { return m_buf.str(); }

private:
    std::ostringstream m_buf;
    std::streambuf* m_old;
};

/* Sets a log level for the lifetime of the object, then puts back the previous one */
class LevelOverride {
public:
    explicit LevelOverride(cdrp::LogLevel level)
        : m_previous(cdrp::Logger::instance().level())
    {
        cdrp::Logger::instance().setLevel(level);
    }

    ~LevelOverride()
    {
        cdrp::Logger::instance().setLevel(m_previous);
    }

private:
    cdrp::LogLevel m_previous;
};

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

size_t countOf(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

} // namespace

using namespace cdrp;

TEST_CASE("logger_emits_level_and_message")
{
    const LevelOverride level(LogLevel::Info);

    CerrCapture out;
    logInfo("Test", "hello");

    CHECK(contains(out.str(), "[INFO]"));
    CHECK(contains(out.str(), "hello"));
}

TEST_CASE("logger_emits_component_before_the_message")
{
    const LevelOverride level(LogLevel::Info);

    CerrCapture out;
    logInfo("Test", "hello");

    CHECK(contains(out.str(), "[Test] -> hello"));
}

TEST_CASE("logger_drops_messages_below_level")
{
    const LevelOverride level(LogLevel::Error);

    CerrCapture out;
    logDebug("Test", "d");
    logInfo("Test", "i");
    logWarn("Test", "w");

    CHECK(out.str().empty());
}

TEST_CASE("logger_emits_at_or_above_level")
{
    const LevelOverride level(LogLevel::Warning);

    CerrCapture out;
    logWarn("Test", "w");
    logError("Test", "e");

    CHECK(contains(out.str(), "[WARN]"));
    CHECK(contains(out.str(), "[ERROR]"));
}

TEST_CASE("logger_colors_each_level")
{
    const LevelOverride level(LogLevel::Debug);

    CerrCapture out;
    logDebug("Test", "d");
    logInfo("Test", "i");
    logWarn("Test", "w");
    logError("Test", "e");

    CHECK(contains(out.str(), "\033[90m[DEBUG]\033[0m"));
    CHECK(contains(out.str(), "\033[32m[INFO]\033[0m"));
    CHECK(contains(out.str(), "\033[33m[WARN]\033[0m"));
    CHECK(contains(out.str(), "\033[31m[ERROR]\033[0m"));
}

TEST_CASE("logger_prefixes_a_timestamp")
{
    const LevelOverride level(LogLevel::Info);

    CerrCapture out;
    logInfo("Test", "x");

    CHECK(std::regex_search(out.str(), std::regex(R"(\d{2}-\d{2} \d{2}:\d{2}:\d{2})")));
}

TEST_CASE("logger_writes_one_line_per_message")
{
    const LevelOverride level(LogLevel::Info);

    CerrCapture out;
    logInfo("Test", "a");
    logInfo("Test", "b");

    const std::string text = out.str();
    CHECK(std::count(text.begin(), text.end(), '\n') == 2);
}

TEST_CASE("logger_does_not_interleave_across_threads")
{
    const LevelOverride level(LogLevel::Info);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 100;

    CerrCapture out;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([] {
            for (int j = 0; j < kPerThread; ++j) {
                logInfo("Test", "concurrent");
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const std::string text = out.str();
    CHECK(std::count(text.begin(), text.end(), '\n') == kThreads * kPerThread);
    CHECK(countOf(text, "[INFO]") == kThreads * kPerThread);
    CHECK(countOf(text, "concurrent") == kThreads * kPerThread);
}
