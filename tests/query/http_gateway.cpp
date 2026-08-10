#include "doctest.h"
#include "constants.hpp"
#include "query/http_gateway.hpp"
#include "query/iquery_store.hpp"
#include "query/services/path_service.hpp"
#include "query/services/query_service.hpp"
#include "query/services/rank_service.hpp"
#include "query/services/stats_service.hpp"

#include "httplib.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace cdrp;

/* The subscribers the tests read, one pair in contact */
const std::string kFirst = "972500000001";
const std::string kSecond = "972500000002";
const std::string kThird = "972500000003";
const std::string kStranger = "972500000009";

/* The operator the tests read */
const std::string kOperator = "42502";

/* How long a test waits for the listener before it gives up */
constexpr int kReadyMillis = 5000;

/* A store answering out of a map */
class FakeStore : public IQueryStore {
public:
    bool hgetall(const std::string_view key, Fields& out) const override
    {
        out.clear();
        const auto found = keys.find(std::string(key));
        if (found != keys.end()) {
            out = found->second;
        }
        return true;
    }

    bool hkeys(const std::string_view key, std::vector<std::string>& out) const override
    {
        out.clear();
        const auto found = keys.find(std::string(key));
        if (found != keys.end()) {
            for (const auto& field : found->second) {
                out.push_back(field.first);
            }
        }
        return true;
    }

    bool hmget(const std::string_view key, const std::vector<std::string>& field_names,
               std::vector<std::string>& out) const override
    {
        out.clear();
        const auto found = keys.find(std::string(key));
        for (const std::string& name : field_names) {
            std::string value;
            if (found != keys.end()) {
                for (const auto& field : found->second) {
                    if (field.first == name) {
                        value = field.second;
                        break;
                    }
                }
            }
            out.push_back(value);
        }
        return true;
    }

    bool dbsize(uint64_t& out) const override
    {
        out = keys.size();
        return true;
    }

    bool top(std::string_view board, std::size_t offset, std::size_t limit,
             Ranked& out, uint64_t& count) const override
    {
        out.clear();
        const auto found = boards.find(std::string(board));
        count = found == boards.end() ? 0 : found->second.size();
        if (found == boards.end() || offset >= found->second.size()) {
            return true;
        }

        const std::size_t left = found->second.size() - offset;
        const std::size_t taken = limit == 0 ? left : std::min(limit, left);
        out.assign(found->second.begin() + static_cast<std::ptrdiff_t>(offset),
                   found->second.begin() + static_cast<std::ptrdiff_t>(offset + taken));
        return true;
    }

    /* Adds one member to one board, the board made when it is written to first */
    void rank(std::string_view board, const std::string& member, uint64_t score)
    {
        boards[std::string(board)].emplace_back(member, score);
    }

    /* Adds one field to one key, the key made when it is written to first */
    void put(const std::string& key, const std::string& field, const std::string& value)
    {
        keys[key].emplace_back(field, value);
    }

    /* Adds both directions of one pair, so the links read the way the writer left them */
    void link(const std::string& first, const std::string& second, const std::string& dur,
              const std::string& sms)
    {
        put(std::string(kLinkPrefix) + first, second + std::string(kFieldDurSuffix), dur);
        put(std::string(kLinkPrefix) + first, second + std::string(kFieldSmsSuffix), sms);
        put(std::string(kLinkPrefix) + second, first + std::string(kFieldDurSuffix), dur);
        put(std::string(kLinkPrefix) + second, first + std::string(kFieldSmsSuffix), sms);
    }

    std::map<std::string, Fields> keys;
    std::map<std::string, Ranked> boards;
};

/* A store holding one subscriber, one operator, the pair first - second, and one board */
FakeStore seeded()
{
    FakeStore store;
    const std::string sub = std::string(kSubPrefix) + kFirst;
    store.put(sub, std::string(kFieldVoiceOut), "60");
    store.put(sub, std::string(kFieldVoiceIn), "40");
    store.put(sub, std::string(kFieldSmsOut), "3");
    store.put(sub, std::string(kFieldSmsIn), "2");

    const std::string op = std::string(kOpPrefix) + kOperator;
    store.put(op, std::string(kFieldVoiceOut), "600");
    store.put(op, std::string(kFieldSmsOut), "30");

    store.put(std::string(kTotalKey), std::string(kFieldRecords), "8");

    store.link(kFirst, kSecond, "60", "3");
    store.link(kFirst, kThird, "10", "9");

    store.rank(kVoiceBoard, kFirst, 100);
    store.rank(kVoiceBoard, kSecond, 60);
    return store;
}

/* The address the tests bind, a port of their own so a running gateway is untouched */
const std::string kHost = "127.0.0.1";
constexpr int kAnyPort = 0;

/* A client of one port, every wait of it bounded */
httplib::Client client(int port)
{
    httplib::Client cli(kHost, port);
    cli.set_connection_timeout(0, 200 * 1000);
    cli.set_read_timeout(2, 0);
    return cli;
}

/* True once the port answers, false when it did not within kReadyMillis */
bool ready(int port)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(kReadyMillis);
    while (std::chrono::steady_clock::now() < deadline) {
        httplib::Client cli = client(port);
        if (cli.Get("/ready")) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

/* A gateway listening for as long as it is alive, on the port it was given */
class Listening {
public:
    Listening(const IQueryStore& store, int port = kAnyPort)
        : m_gateway(store, port, kHost)
        , m_bound(m_gateway.start())
    {
    }

    ~Listening()
    {
        ready(port());
        m_gateway.stop();
    }

    Listening(const Listening&) = delete;
    Listening& operator=(const Listening&) = delete;

    /* What start() returned */
    bool bound() const
    {
        return m_bound;
    }

    /* The port it bound */
    int port() const
    {
        return m_gateway.port();
    }

private:
    HttpGateway m_gateway;
    bool m_bound = false;
};

/* True when the body holds the text, so a test can name one field of it */
bool holds(const std::string& body, const std::string& text)
{
    return body.find(text) != std::string::npos;
}

} // namespace

TEST_CASE("http_gateway_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<HttpGateway>::value);
    CHECK_FALSE(std::is_copy_assignable<HttpGateway>::value);
}

TEST_CASE("http_gateway_answers_a_subscriber_with_the_service_body")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/msisdn/" + kFirst);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->get_header_value("Content-Type") == "application/json");
    CHECK(res->body == QueryService(store).msisdn(kFirst).body);
    CHECK(holds(res->body, R"("voice-out":60)"));
}

TEST_CASE("http_gateway_answers_an_operator_with_the_service_body")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/operator/" + kOperator);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == QueryService(store).op(kOperator).body);
}

TEST_CASE("http_gateway_answers_one_msisdn_under_link_with_its_peers")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(holds(res->body, kSecond));
    CHECK(holds(res->body, R"("count":2)"));
    CHECK(holds(res->body, R"("limit":)" + std::to_string(kPeerLimit)));
}

TEST_CASE("http_gateway_reads_the_peer_parameters_off_the_query_string")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst + "?weights=1&sort=sms&limit=1");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(holds(res->body, R"("sort":"sms")"));
    CHECK(holds(res->body, R"("limit":1)"));
    CHECK(holds(res->body, R"({"msisdn":"972500000003","duration":10,"sms":9})"));
    CHECK_FALSE(holds(res->body, R"("msisdn":"972500000002")"));
}

TEST_CASE("http_gateway_sends_a_400_for_a_parameter_it_refuses")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst + "?limit=ten");

    REQUIRE(res);
    CHECK(res->status == 400);
    CHECK(holds(res->body, "limit"));
}

TEST_CASE("http_gateway_answers_two_msisdns_under_link_with_what_they_exchanged")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst + "/" + kSecond);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == QueryService(store).link(kFirst, kSecond).body);
}

TEST_CASE("http_gateway_answers_a_path_between_two_msisdns")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/path/" + kFirst + "/" + kSecond);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == PathService(store).path(kFirst, kSecond, false).body);
}

TEST_CASE("http_gateway_adds_the_hops_of_a_path_when_they_are_asked_for")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/path/" + kFirst + "/" + kSecond + "?weights=1");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(holds(res->body, R"("hops":[)"));
}

TEST_CASE("http_gateway_answers_health_with_the_store_state")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/health");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == StatsService(store).health().body);
    CHECK(holds(res->body, R"("status":"ok")"));
    CHECK(holds(res->body, R"("store":"up")"));
}

TEST_CASE("http_gateway_answers_totals_with_the_lifetime_counters")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/totals");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == StatsService(store).totals().body);
    CHECK(holds(res->body, R"("records":8)"));
}

TEST_CASE("http_gateway_answers_a_board_with_one_page_of_it")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/top/voice");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(holds(res->body, R"("board":"voice")"));
    CHECK(holds(res->body, R"("count":2)"));
    CHECK(holds(res->body, R"("limit":)" + std::to_string(kTopLimit)));
    CHECK(holds(res->body, R"({"id":"972500000001","score":100})"));
}

TEST_CASE("http_gateway_reads_the_board_parameters_off_the_query_string")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/top/voice?limit=1&offset=1");

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(holds(res->body, R"("offset":1)"));
    CHECK(holds(res->body, R"({"id":"972500000002","score":60})"));
    CHECK_FALSE(holds(res->body, R"("id":"972500000001")"));
}

TEST_CASE("http_gateway_sends_a_400_for_a_board_that_does_not_exist")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/top/calls");

    REQUIRE(res);
    CHECK(res->status == 400);
}

TEST_CASE("http_gateway_passes_the_service_404_on")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/msisdn/" + kStranger);

    REQUIRE(res);
    CHECK(res->status == 404);
    CHECK(res->body == QueryService(store).msisdn(kStranger).body);
}

TEST_CASE("http_gateway_sends_a_json_404_for_a_route_it_does_not_serve")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/nothing/here");

    REQUIRE(res);
    CHECK(res->status == 404);
    CHECK(holds(res->body, "no such route"));
}

TEST_CASE("http_gateway_serves_no_route_for_a_parameter_that_is_not_a_number")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/msisdn/not_a_number");

    REQUIRE(res);
    CHECK(res->status == 404);
    CHECK(holds(res->body, "no such route"));
}

TEST_CASE("http_gateway_answers_several_connections_at_once")
{
    const FakeStore store = seeded();
    const Listening gateway(store);
    REQUIRE(ready(gateway.port()));

    constexpr int kCallers = 8;
    std::vector<std::thread> callers;
    std::vector<int> statuses(kCallers, 0);

    for (int i = 0; i < kCallers; ++i) {
        callers.emplace_back([i, &statuses, port = gateway.port()] {
            httplib::Client cli = client(port);
            const auto res = cli.Get("/query/msisdn/" + kFirst);
            statuses[i] = res ? res->status : 0;
        });
    }
    for (std::thread& caller : callers) {
        caller.join();
    }

    for (const int status : statuses) {
        CHECK(status == 200);
    }
}

TEST_CASE("http_gateway_start_returns_true_once_the_port_is_bound")
{
    const FakeStore store = seeded();

    HttpGateway gateway(store, kAnyPort, kHost);
    const bool bound = gateway.start();

    REQUIRE(ready(gateway.port()));
    gateway.stop();

    CHECK(bound);
    CHECK(gateway.port() != kAnyPort);
}

TEST_CASE("http_gateway_start_returns_false_when_the_port_is_taken")
{
    const FakeStore store = seeded();
    const Listening first(store);
    REQUIRE(first.bound());
    REQUIRE(ready(first.port()));

    const Listening second(store, first.port());

    CHECK_FALSE(second.bound());
}
