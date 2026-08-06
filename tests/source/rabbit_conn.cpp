#include "doctest.h"
#include "source/rabbit_conn.hpp"

#include <chrono>
#include <string>
#include <type_traits>

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

TEST_CASE("rabbit_conn_cannot_be_copied")
{
    CHECK_FALSE(std::is_copy_constructible<RabbitConn>::value);
    CHECK_FALSE(std::is_copy_assignable<RabbitConn>::value);
}

TEST_CASE("rabbit_conn_starts_with_an_empty_message")
{
    const RabbitConn::Message message;

    CHECK(message.body.empty());
    CHECK(message.type.empty());
    CHECK(message.tag == 0);
}

TEST_CASE("rabbit_conn_closes_cleanly_when_it_was_never_opened")
{
    RabbitConn conn;
    CHECK(true);
}

TEST_CASE("rabbit_conn_fails_to_open_a_url_it_cannot_parse")
{
    RabbitConn conn;

    for (const char* url : { "", "not a url", "http://127.0.0.1:5672/", "amqp://" }) {
        CHECK_FALSE(conn.open(url, "cdr.q"));
    }
}

TEST_CASE("rabbit_conn_fails_to_open_when_nothing_listens")
{
    RabbitConn conn;
    bool opened = true;

    const long long elapsed = millisOf([&] { opened = conn.open(kDeadUrl, "cdr.q"); });

    CHECK_FALSE(opened);
    CHECK(elapsed < 5000);
}

TEST_CASE("rabbit_conn_survives_a_second_open_after_a_failed_one")
{
    RabbitConn conn;

    CHECK_FALSE(conn.open(kDeadUrl, "cdr.q"));
    CHECK_FALSE(conn.open(kDeadUrl, "cdr.q"));
}

TEST_CASE("rabbit_conn_fails_to_consume_before_it_is_open")
{
    RabbitConn conn;
    RabbitConn::Message message;

    const long long elapsed = millisOf([&] { CHECK(conn.consume(message, 50) == RabbitConn::Status::FAIL); });

    CHECK(elapsed < 5000);
    CHECK(message.body.empty());
    CHECK(message.tag == 0);
}

TEST_CASE("rabbit_conn_fails_to_consume_after_a_failed_open")
{
    RabbitConn conn;
    REQUIRE_FALSE(conn.open(kDeadUrl, "cdr.q"));

    RabbitConn::Message message;
    CHECK(conn.consume(message, 50) == RabbitConn::Status::FAIL);
}

TEST_CASE("rabbit_conn_fails_to_consume_with_no_timeout_left")
{
    RabbitConn conn;
    RabbitConn::Message message;

    CHECK(conn.consume(message, 0) == RabbitConn::Status::FAIL);
}

TEST_CASE("rabbit_conn_fails_to_ack_before_it_is_open")
{
    RabbitConn conn;

    CHECK_FALSE(conn.ack(1, false));
    CHECK_FALSE(conn.ack(1, true));
}

TEST_CASE("rabbit_conn_fails_to_ack_after_a_failed_open")
{
    RabbitConn conn;
    REQUIRE_FALSE(conn.open(kDeadUrl, "cdr.q"));

    CHECK_FALSE(conn.ack(0, false));
}
