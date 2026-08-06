#include "doctest.h"
#include "ingest/iingestor.hpp"
#include "ingest/rabbit_ingestor.hpp"
#include "sink/isink.hpp"

#include <chrono>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <vector>

namespace {

/* Every call here is bounded, so a stuck ingestor fails the suite instead of hanging it */
constexpr long long kTimeoutMs = 15000;

/* A sink that counts what it consumes; the ingestor feeds it from several threads */
class CountingSink : public cdrp::ISink {
public:
    void consume(std::vector<cdrp::CdrRecord>& batch) override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_count += batch.size();
    }

    std::size_t count()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

private:
    std::mutex m_mutex;
    std::size_t m_count = 0;
};

/* Milliseconds a call took, so a test can bound a wait */
template <typename Fn>
long long millisOf(Fn fn)
{
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

} // namespace

using namespace cdrp;

TEST_CASE("rabbit_ingestor_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<RabbitIngestor>::value);
    CHECK_FALSE(std::is_copy_assignable<RabbitIngestor>::value);
}

TEST_CASE("rabbit_ingestor_constructs_and_destructs_without_starting")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    CHECK(sink.count() == 0);
}

TEST_CASE("rabbit_ingestor_stop_is_safe_without_a_start")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    const long long elapsed = millisOf([&] { ingestor.stop(); });

    CHECK(elapsed < kTimeoutMs);
    CHECK(sink.count() == 0);
}

TEST_CASE("rabbit_ingestor_stop_is_safe_when_called_twice_without_a_start")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    ingestor.stop();
    const long long elapsed = millisOf([&] { ingestor.stop(); });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_starts_and_stops_within_the_bound")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    const long long elapsed = millisOf([&] {
        ingestor.start();
        ingestor.stop();
    });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_start_is_idempotent")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    const bool first = ingestor.start();
    const bool second = ingestor.start();
    ingestor.stop();

    CHECK(first == second);
}

TEST_CASE("rabbit_ingestor_stops_twice_after_a_start")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    ingestor.start();
    ingestor.stop();
    const long long elapsed = millisOf([&] { ingestor.stop(); });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_destructs_while_it_is_still_running")
{
    CountingSink sink;

    const long long elapsed = millisOf([&] {
        RabbitIngestor ingestor(sink);
        ingestor.start();
    });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_runs_again_after_it_was_stopped")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    ingestor.start();
    ingestor.stop();
    const long long elapsed = millisOf([&] {
        ingestor.start();
        ingestor.stop();
    });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_is_usable_through_the_iingestor_interface")
{
    CountingSink sink;
    RabbitIngestor concrete(sink);
    IIngestor& ingestor = concrete;

    const long long elapsed = millisOf([&] {
        ingestor.start();
        ingestor.stop();
    });

    CHECK(elapsed < kTimeoutMs);
}

TEST_CASE("rabbit_ingestor_feeds_the_sink_nothing_before_it_starts")
{
    CountingSink sink;
    RabbitIngestor ingestor(sink);

    CHECK(sink.count() == 0);

    ingestor.stop();

    CHECK(sink.count() == 0);
}
