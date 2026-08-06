#include "doctest.h"
#include "source/icdr_source.hpp"
#include "source/rabbit_conn.hpp"
#include "source/rabbit_source.hpp"

#include <chrono>
#include <string>
#include <type_traits>
#include <vector>

namespace {

/* A loopback endpoint nothing listens on, so a connect attempt is refused at once */
const std::string kDeadUrl = "amqp://guest:guest@127.0.0.1:1/%2f";

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

TEST_CASE("rabbit_source_cannot_be_copied")
{
    CHECK_FALSE(std::is_copy_constructible<RabbitSource>::value);
    CHECK_FALSE(std::is_copy_assignable<RabbitSource>::value);
}

TEST_CASE("rabbit_source_is_usable_through_the_source_interface")
{
    CHECK(std::is_convertible<RabbitSource*, ICdrSource*>::value);
}

TEST_CASE("rabbit_source_starts_with_empty_counters")
{
    RabbitConn conn;
    RabbitSource source(conn);

    CHECK(source.parsed() == 0);
    CHECK(source.rejected() == 0);
    CHECK(source.last_tag() == 0);
}

TEST_CASE("rabbit_source_fails_when_the_connection_was_never_opened")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out;
    ICdrSource::Status status = ICdrSource::Status::OK;

    const long long elapsed = millisOf([&] { status = source.next(out); });

    CHECK(status == ICdrSource::Status::FAIL);
    CHECK(out.empty());
    CHECK(elapsed < 5000);
}

TEST_CASE("rabbit_source_fails_after_the_connection_failed_to_open")
{
    RabbitConn conn;
    REQUIRE_FALSE(conn.open(kDeadUrl, "cdr.q", 16));

    RabbitSource source(conn);
    std::vector<CdrRecord> out;

    CHECK(source.next(out) == ICdrSource::Status::FAIL);
    CHECK(out.empty());
}

TEST_CASE("rabbit_source_keeps_failing_over_repeated_calls_on_a_dead_connection")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out;

    const long long elapsed = millisOf([&] {
        for (int i = 0; i < 3; ++i) {
            CHECK(source.next(out) == ICdrSource::Status::FAIL);
        }
    });

    CHECK(elapsed < 5000);
}

TEST_CASE("rabbit_source_counts_nothing_when_no_message_arrives")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out;

    source.next(out);

    CHECK(source.parsed() == 0);
    CHECK(source.rejected() == 0);
    CHECK(source.last_tag() == 0);
}

TEST_CASE("rabbit_source_is_done_once_it_is_stopped")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out;
    ICdrSource::Status status = ICdrSource::Status::OK;

    source.stop();
    const long long elapsed = millisOf([&] { status = source.next(out); });

    CHECK(status == ICdrSource::Status::DONE);
    CHECK(out.empty());
    CHECK(elapsed < 5000);
}

TEST_CASE("rabbit_source_stays_done_after_a_second_stop")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out;

    source.stop();
    source.stop();

    CHECK(source.next(out) == ICdrSource::Status::DONE);
}

TEST_CASE("rabbit_source_clears_the_records_the_caller_already_had")
{
    RabbitConn conn;
    RabbitSource source(conn);
    std::vector<CdrRecord> out(2);

    source.stop();
    source.next(out);

    CHECK(out.empty());
}

TEST_CASE("rabbit_source_is_reachable_through_a_source_reference")
{
    RabbitConn conn;
    RabbitSource source(conn);
    ICdrSource& base = source;
    std::vector<CdrRecord> out;

    CHECK(base.next(out) == ICdrSource::Status::FAIL);
}
