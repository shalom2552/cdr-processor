#include "doctest.h"
#include "constants.hpp"
#include "query/services/query_service.hpp"

#include <cstdint>
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

    bool dbsize(uint64_t& out) const override
    {
        out = keys.size();
        return storeUp;
    }

    bool top(std::string_view, std::size_t, std::size_t, Ranked& out, uint64_t& count) const override
    {
        out.clear();
        count = 0;
        return storeUp;
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

/* Parameters asking for every peer, unweighted, the way a plain call does */
QueryParams plain()
{
    return QueryParams();
}

/* Parameters asking for weighted peers by one metric */
QueryParams weighted(Sort sort, std::size_t offset = 0, std::size_t limit = 0)
{
    QueryParams params;
    params.weights = true;
    params.sort = sort;
    params.offset = offset;
    params.limit = limit;
    return params;
}

/* True when the body holds the text, so a test can name one field of it */
bool holds(const std::string& body, const std::string& text)
{
    return body.find(text) != std::string::npos;
}

} // namespace

using namespace cdrp;

TEST_CASE("query_service_answers_a_subscriber_with_its_counters")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.msisdn(kFirst);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("voice-out":60)"));
    CHECK(holds(result.body, R"("voice-in":40)"));
    CHECK(holds(result.body, R"("sms-out":3)"));
    CHECK(holds(result.body, R"("sms-in":2)"));
}

TEST_CASE("query_service_answers_a_subscriber_data_in_bytes")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.msisdn(kFirst);

    CHECK(holds(result.body, R"("data-in":2048)"));
    CHECK(holds(result.body, R"("data-out":1024)"));
}

TEST_CASE("query_service_answers_a_subscriber_with_its_unanswered_calls")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.msisdn(kFirst);

    CHECK(holds(result.body, R"("no-answer":1)"));
    CHECK(holds(result.body, R"("busy":0)"));
    CHECK(holds(result.body, R"("failed":0)"));
}

TEST_CASE("query_service_answers_404_for_a_subscriber_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.msisdn(kStranger);

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

    const Result result = service.msisdn(kFirst);

    CHECK(result.status == 503);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("query_service_answers_an_operator_with_its_traffic")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.op(kOperator);

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

    const Result result = service.peers(kSecond, plain());

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
    CHECK(holds(result.body, kThird));
    CHECK(result.body.find(kFirst) == result.body.rfind(kFirst));
    CHECK(result.body.find(kThird) == result.body.rfind(kThird));
}

TEST_CASE("query_service_answers_the_peers_of_a_subscriber_with_their_count")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kSecond, plain());

    CHECK(holds(result.body, R"("count":2)"));
    CHECK(holds(result.body, R"("offset":0)"));
}

TEST_CASE("query_service_answers_404_for_the_peers_of_a_subscriber_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kStranger, plain());

    CHECK(result.status == 404);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("query_service_answers_503_for_peers_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.peers(kFirst, plain()).status == 503);
}

TEST_CASE("query_service_answers_weighted_peers_with_what_each_exchanged")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kSecond, weighted(Sort::Duration));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("sort":"dur")"));
    CHECK(holds(result.body, R"({"msisdn":"972500000003","duration":90,"sms":5})"));
    CHECK(holds(result.body, R"({"msisdn":"972500000001","duration":60,"sms":3})"));
}

TEST_CASE("query_service_orders_weighted_peers_by_duration")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kSecond, weighted(Sort::Duration));

    CHECK(result.body.find(kThird) < result.body.find(kFirst));
}

TEST_CASE("query_service_orders_weighted_peers_by_sms")
{
    FakeStore store = seeded();
    store.link(kSecond, kStranger, "10", "99");
    const QueryService service(store);

    const Result result = service.peers(kSecond, weighted(Sort::Sms));

    CHECK(holds(result.body, R"("sort":"sms")"));
    CHECK(result.body.find(kStranger) < result.body.find(kThird));
    CHECK(result.body.find(kThird) < result.body.find(kFirst));
}

TEST_CASE("query_service_breaks_a_tie_on_the_sort_metric_by_the_other_one")
{
    FakeStore store;
    store.link(kFirst, kSecond, "60", "1");
    store.link(kFirst, kThird, "60", "9");
    const QueryService service(store);

    const Result result = service.peers(kFirst, weighted(Sort::Duration));

    CHECK(result.body.find(kThird) < result.body.find(kSecond));
}

TEST_CASE("query_service_breaks_a_tie_on_both_metrics_by_msisdn")
{
    FakeStore store;
    store.link(kFirst, kThird, "60", "3");
    store.link(kFirst, kSecond, "60", "3");
    const QueryService service(store);

    const Result result = service.peers(kFirst, weighted(Sort::Duration));

    CHECK(result.body.find(kSecond) < result.body.find(kThird));
}

TEST_CASE("query_service_pages_weighted_peers")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kSecond, weighted(Sort::Duration, 1, 1));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("count":2)"));
    CHECK(holds(result.body, R"("offset":1)"));
    CHECK(holds(result.body, R"("limit":1)"));
    CHECK(holds(result.body, kFirst));
    CHECK_FALSE(holds(result.body, R"("msisdn":"972500000003")"));
}

TEST_CASE("query_service_answers_no_peers_for_an_offset_past_the_end")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.peers(kSecond, weighted(Sort::Duration, 9, 10));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("count":2)"));
    CHECK(holds(result.body, R"("peers":[])"));
}

TEST_CASE("query_service_pages_the_peers_of_an_unweighted_call")
{
    const FakeStore store = seeded();
    const QueryService service(store);
    QueryParams params;
    params.limit = 1;

    const Result result = service.peers(kSecond, params);

    CHECK(holds(result.body, R"("count":2)"));
    CHECK(holds(result.body, R"("peers":["972500000001"])"));
}

TEST_CASE("query_service_answers_404_for_weighted_peers_of_a_subscriber_never_seen")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    CHECK(service.peers(kStranger, weighted(Sort::Duration)).status == 404);
}

TEST_CASE("query_service_answers_503_for_weighted_peers_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const QueryService service(store);

    CHECK(service.peers(kFirst, weighted(Sort::Duration)).status == 503);
}

TEST_CASE("query_service_answers_a_link_with_what_the_pair_exchanged")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result result = service.link(kFirst, kSecond);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("duration":60)"));
    CHECK(holds(result.body, R"("sms":3)"));
}

TEST_CASE("query_service_answers_the_same_counters_both_ways_round")
{
    const FakeStore store = seeded();
    const QueryService service(store);

    const Result forward = service.link(kFirst, kSecond);
    const Result backward = service.link(kSecond, kFirst);

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

TEST_CASE("query_service_reads_the_store_it_was_handed")
{
    FakeStore store = seeded();
    const QueryService service(store);

    service.msisdn(kFirst);

    CHECK(store.reads > 0);
}
