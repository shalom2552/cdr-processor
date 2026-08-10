#include "doctest.h"
#include "constants.hpp"
#include "query/http_gateway.hpp"
#include "query/query_service.hpp"
#include "query/iquery_store.hpp"

#include "httplib.h"
#include <chrono>
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
};

/* A store holding one subscriber, one operator, and the pair first - second */
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

    store.link(kFirst, kSecond, "60", "3");
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
    Listening(const QueryService& service, int port = kAnyPort)
        : m_gateway(service, port, kHost)
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
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/msisdn/" + kFirst);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->get_header_value("Content-Type") == "application/json");
    CHECK(res->body == service.msisdn(kFirst).body);
    CHECK(holds(res->body, R"("voice-out":60)"));
}

TEST_CASE("http_gateway_answers_an_operator_with_the_service_body")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/operator/" + kOperator);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == service.op(kOperator).body);
}

TEST_CASE("http_gateway_answers_one_msisdn_under_link_with_its_peers")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == service.peers(kFirst).body);
    CHECK(holds(res->body, kSecond));
}

TEST_CASE("http_gateway_answers_two_msisdns_under_link_with_what_they_exchanged")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/link/" + kFirst + "/" + kSecond);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == service.link(kFirst, kSecond).body);
}

TEST_CASE("http_gateway_answers_a_path_between_two_msisdns")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/path/" + kFirst + "/" + kSecond);

    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == service.path(kFirst, kSecond).body);
}

TEST_CASE("http_gateway_passes_the_service_404_on")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
    REQUIRE(ready(gateway.port()));

    httplib::Client cli = client(gateway.port());
    const auto res = cli.Get("/query/msisdn/" + kStranger);

    REQUIRE(res);
    CHECK(res->status == 404);
    CHECK(res->body == service.msisdn(kStranger).body);
}

TEST_CASE("http_gateway_sends_a_json_404_for_a_route_it_does_not_serve")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening gateway(service);
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
    const QueryService service(store);
    const Listening gateway(service);
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
    const QueryService service(store);
    const Listening gateway(service);
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
    const QueryService service(store);

    HttpGateway gateway(service, kAnyPort, kHost);
    const bool bound = gateway.start();

    REQUIRE(ready(gateway.port()));
    gateway.stop();

    CHECK(bound);
    CHECK(gateway.port() != kAnyPort);
}

TEST_CASE("http_gateway_start_returns_false_when_the_port_is_taken")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    const Listening first(service);
    REQUIRE(first.bound());
    REQUIRE(ready(first.port()));

    const Listening second(service, first.port());

    CHECK_FALSE(second.bound());
}
