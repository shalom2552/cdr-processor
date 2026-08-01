#include "doctest.h"
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
    Logger::instance().setLevel(LogLevel::Debug);

    CerrCapture out;
    logInfo("hello");

    CHECK(contains(out.str(), "[INFO]"));
    CHECK(contains(out.str(), "hello"));
}

TEST_CASE("logger_drops_messages_below_level")
{
    Logger::instance().setLevel(LogLevel::Error);

    CerrCapture out;
    logDebug("d");
    logInfo("i");
    logWarn("w");

    CHECK(out.str().empty());
}

TEST_CASE("logger_emits_at_or_above_level")
{
    Logger::instance().setLevel(LogLevel::Warning);

    CerrCapture out;
    logWarn("w");
    logError("e");

    CHECK(contains(out.str(), "[WARN]"));
    CHECK(contains(out.str(), "[ERROR]"));
}

TEST_CASE("logger_colors_each_level")
{
    Logger::instance().setLevel(LogLevel::Debug);

    CerrCapture out;
    logDebug("d");
    logInfo("i");
    logWarn("w");
    logError("e");

    CHECK(contains(out.str(), "\033[90m[DEBUG]\033[0m"));
    CHECK(contains(out.str(), "\033[32m[INFO]\033[0m"));
    CHECK(contains(out.str(), "\033[33m[WARN]\033[0m"));
    CHECK(contains(out.str(), "\033[31m[ERROR]\033[0m"));
}

TEST_CASE("logger_prefixes_a_timestamp")
{
    Logger::instance().setLevel(LogLevel::Debug);

    CerrCapture out;
    logInfo("x");

    CHECK(std::regex_search(out.str(), std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")));
}

TEST_CASE("logger_writes_one_line_per_message")
{
    Logger::instance().setLevel(LogLevel::Debug);

    CerrCapture out;
    logInfo("a");
    logInfo("b");

    const std::string text = out.str();
    CHECK(std::count(text.begin(), text.end(), '\n') == 2);
}

TEST_CASE("logger_does_not_interleave_across_threads")
{
    Logger::instance().setLevel(LogLevel::Debug);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 100;

    CerrCapture out;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([] {
            for (int j = 0; j < kPerThread; ++j) {
                logInfo("concurrent");
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
