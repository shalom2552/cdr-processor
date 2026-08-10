#include "doctest.h"
#include "constants.hpp"
#include "query/query_service.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace cdrp;

/* The subscribers the tests read, one chain of three */
const std::string kFirst = "972500000001";
const std::string kSecond = "972500000002";
const std::string kThird = "972500000003";
const std::string kStranger = "972500000009";

/* The operator the tests read */
const std::string kOperator = "42502";

/* A store answering out of a map, failing every read when a test says so */
class FakeStore : public IQueryStore {
public:
    bool hgetall(const std::string_view key, Fields& out) const override
    {
        ++reads;
        out.clear();
        if (!storeUp) return false;
        const auto found = keys.find(std::string(key));
        if (found != keys.end()) {
            out = found->second;
        }
        return true;
    }

    bool hkeys(const std::string_view key, std::vector<std::string>& out) const override
    {
        ++reads;
        out.clear();
        if (!storeUp) return false;
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
        ++reads;
        out.clear();
        if (!storeUp) return false;
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
    bool storeUp = true;
    mutable std::size_t reads = 0;
};

/* A store holding one subscriber, one operator, and the chain first - second - third */
FakeStore seeded()
{
    FakeStore store;
    const std::string sub = std::string(kSubPrefix) + kFirst;
    store.put(sub, std::string(kFieldVoiceOut), "60");
    store.put(sub, std::string(kFieldVoiceIn), "40");
    store.put(sub, std::string(kFieldDataRx), "2048");
    store.put(sub, std::string(kFieldDataTx), "1024");
    store.put(sub, std::string(kFieldSmsOut), "3");
    store.put(sub, std::string(kFieldSmsIn), "2");
    store.put(sub, std::string(kFieldNoans), "1");
    store.put(sub, std::string(kFieldBusy), "0");
    store.put(sub, std::string(kFieldFailed), "0");

    const std::string op = std::string(kOpPrefix) + kOperator;
    store.put(op, std::string(kFieldVoiceOut), "600");
    store.put(op, std::string(kFieldVoiceIn), "400");
    store.put(op, std::string(kFieldSmsOut), "30");
    store.put(op, std::string(kFieldSmsIn), "20");

    store.link(kFirst, kSecond, "60", "3");
    store.link(kSecond, kThird, "90", "5");
    return store;
}

/* True when the body holds the text, so a test can name one field of it */
bool holds(const std::string& body, const std::string& text)
{
    return body.find(text) != std::string::npos;
}

/* Milliseconds a call took, so a test can bound a search */
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

TEST_CASE("query_service_answers_a_subscriber_with_its_counters")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.msisdn(kFirst);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("voice-out":60)"));
    CHECK(holds(result.body, R"("voice-in":40)"));
    CHECK(holds(result.body, R"("sms-out":3)"));
    CHECK(holds(result.body, R"("sms-in":2)"));
}

TEST_CASE("query_service_answers_a_subscriber_data_in_kilobytes")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.msisdn(kFirst);

    CHECK(holds(result.body, R"("data-in":2)"));
    CHECK(holds(result.body, R"("data-out":1)"));
}

TEST_CASE("query_service_answers_a_subscriber_with_its_unanswered_calls")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.msisdn(kFirst);

    CHECK(holds(result.body, R"("no-answer":1)"));
    CHECK(holds(result.body, R"("busy":0)"));
    CHECK(holds(result.body, R"("failed":0)"));
}

TEST_CASE("query_service_answers_404_for_a_subscriber_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.msisdn(kStranger);

    CHECK(result.status == 404);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("query_service_answers_404_for_an_empty_msisdn")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    CHECK(service.msisdn(std::string_view()).status == 404);
}

TEST_CASE("query_service_answers_503_when_the_store_cannot_be_read")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    const QueryService::Result result = service.msisdn(kFirst);

    CHECK(result.status == 503);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("query_service_answers_an_operator_with_its_traffic")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.op(kOperator);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("voice-out":600)"));
    CHECK(holds(result.body, R"("voice-in":400)"));
    CHECK(holds(result.body, R"("sms-out":30)"));
    CHECK(holds(result.body, R"("sms-in":20)"));
}

TEST_CASE("query_service_answers_404_for_an_operator_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    CHECK(service.op("99999").status == 404);
}

TEST_CASE("query_service_answers_503_for_an_operator_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.op(kOperator).status == 503);
}

TEST_CASE("query_service_lists_every_peer_once")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.peers(kSecond);

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
    CHECK(holds(result.body, kThird));
    CHECK(result.body.find(kFirst) == result.body.rfind(kFirst));
    CHECK(result.body.find(kThird) == result.body.rfind(kThird));
}

TEST_CASE("query_service_answers_404_for_the_peers_of_a_subscriber_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.peers(kStranger);

    CHECK(result.status == 404);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("query_service_answers_503_for_peers_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.peers(kFirst).status == 503);
}

TEST_CASE("query_service_answers_a_link_with_what_the_pair_exchanged")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.link(kFirst, kSecond);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("duration":60)"));
    CHECK(holds(result.body, R"("sms":3)"));
}

TEST_CASE("query_service_answers_the_same_counters_both_ways_round")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result forward = service.link(kFirst, kSecond);
    const QueryService::Result backward = service.link(kSecond, kFirst);

    CHECK(forward.status == backward.status);
    CHECK(holds(forward.body, R"("duration":60,"sms":3)"));
    CHECK(holds(backward.body, R"("duration":60,"sms":3)"));
}

TEST_CASE("query_service_answers_404_for_a_pair_never_in_contact")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    CHECK(service.link(kFirst, kThird).status == 404);
}

TEST_CASE("query_service_answers_503_for_a_link_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.link(kFirst, kSecond).status == 503);
}

TEST_CASE("query_service_finds_the_path_of_a_pair_in_contact")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.path(kFirst, kSecond);

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
    CHECK(holds(result.body, kSecond));
}

TEST_CASE("query_service_finds_the_path_over_one_subscriber_between")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.path(kFirst, kThird);

    CHECK(result.status == 200);
    CHECK(result.body.find(kFirst) < result.body.find(kSecond));
    CHECK(result.body.find(kSecond) < result.body.find(kThird));
}

TEST_CASE("query_service_finds_the_path_of_a_subscriber_to_itself")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const QueryService::Result result = service.path(kFirst, kFirst);

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
}

TEST_CASE("query_service_answers_404_for_a_path_that_is_not_there")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    CHECK(service.path(kFirst, kStranger).status == 404);
}

TEST_CASE("query_service_answers_404_for_a_path_longer_than_the_hop_limit")
{
    FakeStore store = seeded();
    std::vector<std::string> chain;
    for (std::size_t hop = 0; hop <= kMaxHops + 4; ++hop) {
        chain.push_back("9725100000" + std::to_string(hop));
    }
    for (std::size_t hop = 1; hop < chain.size(); ++hop) {
        store.link(chain[hop - 1], chain[hop], "10", "1");
    }
    const QueryService service(store);

    CHECK(service.path(chain.front(), chain.back()).status == 404);
}

TEST_CASE("query_service_answers_503_for_a_path_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.path(kFirst, kThird).status == 503);
}

TEST_CASE("query_service_gives_up_on_a_wide_graph_instead_of_searching_it_whole")
{
    FakeStore store;
    const std::size_t peers = 400;
    for (std::size_t index = 0; index < peers; ++index) {
        const std::string peer = "97252" + std::to_string(1000000 + index);
        store.link(kFirst, peer, "10", "1");
        for (std::size_t step = 0; step < 40; ++step) {
            store.link(peer, peer + "x" + std::to_string(step), "10", "1");
        }
    }
    const QueryService service(store);
    QueryService::Result result;

    const long long elapsed = millisOf([&] { result = service.path(kFirst, kStranger); });

    CHECK(result.status == 404);
    CHECK(elapsed < 10000);
}

TEST_CASE("query_service_reads_the_store_it_was_handed")
{
    FakeStore store = seeded();
    const QueryService service(store);

    service.msisdn(kFirst);

    CHECK(store.reads > 0);
}
