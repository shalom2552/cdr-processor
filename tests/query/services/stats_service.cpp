#include "doctest.h"
#include "config.hpp"
#include "constants.hpp"
#include "query/services/stats_service.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace cdrp;

/* A store answering out of a map, failing every read when a test says so */
class FakeStore : public IQueryStore {
public:
    bool hgetall(const std::string_view key, Fields& out) const override
    {
        out.clear();
        if (!storeUp) return false;
        const auto found = keys.find(std::string(key));
        if (found != keys.end()) {
            out = found->second;
        }
        return true;
    }

    bool hkeys(const std::string_view, std::vector<std::string>& out) const override
    {
        out.clear();
        return storeUp;
    }

    bool hmget(const std::string_view, const std::vector<std::string>&,
               std::vector<std::string>& out) const override
    {
        out.clear();
        return storeUp;
    }

    bool dbsize(uint64_t& out) const override
    {
        out = 0;
        if (!storeUp) return false;
        out = size;
        return true;
    }

    bool top(std::string_view, std::size_t, std::size_t, Ranked& out, uint64_t& count) const override
    {
        out.clear();
        count = 0;
        return storeUp;
    }

    /* Adds one field to one key, the key made when it is written to first */
    void put(const std::string& key, std::string_view field, const std::string& value)
    {
        keys[key].emplace_back(std::string(field), value);
    }

    std::map<std::string, Fields> keys;
    uint64_t size = 0;
    bool storeUp = true;
};

/* True when the body holds the text, so a test can name one field of it */
bool holds(const std::string& body, const std::string& text)
{
    return body.find(text) != std::string::npos;
}

} // namespace

using namespace cdrp;

TEST_CASE("stats_service_answers_health_with_the_store_up")
{
    FakeStore store;
    store.size = 214893;
    const StatsService service(store);

    const Result result = service.health();

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("status":"ok")"));
    CHECK(holds(result.body, R"("store":"up")"));
    CHECK(holds(result.body, R"("keys":214893)"));
}

TEST_CASE("stats_service_answers_health_with_the_store_down")
{
    FakeStore store;
    store.size = 214893;
    store.storeUp = false;
    const StatsService service(store);

    const Result result = service.health();

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("store":"down")"));
    CHECK(holds(result.body, R"("keys":0)"));
}

TEST_CASE("stats_service_answers_health_with_the_path_bounds_from_config")
{
    FakeStore store;
    const StatsService service(store);

    const Result result = service.health();

    CHECK(holds(result.body, R"("max-hops":)" + std::to_string(cfg.query.max_hops)));
    CHECK(holds(result.body, R"("max-visited":)" + std::to_string(cfg.query.max_visited)));
}

TEST_CASE("stats_service_answers_totals_of_an_empty_store_with_zeros")
{
    FakeStore store;
    const StatsService service(store);

    const Result result = service.totals();

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("records":0)"));
    CHECK(holds(result.body, R"("data-tx":0)"));
}

TEST_CASE("stats_service_answers_totals_with_every_counter_the_hash_holds")
{
    FakeStore store;
    const std::string key(kTotalKey);
    store.put(key, kFieldRecords, "8");
    store.put(key, kFieldMocCnt, "1");
    store.put(key, kFieldMtcCnt, "2");
    store.put(key, kFieldSmsMoCnt, "3");
    store.put(key, kFieldSmsMtCnt, "4");
    store.put(key, kFieldDataCnt, "5");
    store.put(key, kFieldNoansCnt, "6");
    store.put(key, kFieldBusyCnt, "7");
    store.put(key, kFieldFailedCnt, "9");
    store.put(key, kFieldMocDur, "3314");
    store.put(key, kFieldMtcDur, "120");
    store.put(key, kFieldDataDur, "60");
    store.put(key, kFieldDataRx, "8215");
    store.put(key, kFieldDataTx, "9273");
    const StatsService service(store);

    const Result result = service.totals();

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("records":8)"));
    CHECK(holds(result.body, R"("moc-cnt":1)"));
    CHECK(holds(result.body, R"("mtc-cnt":2)"));
    CHECK(holds(result.body, R"("sms-mo-cnt":3)"));
    CHECK(holds(result.body, R"("sms-mt-cnt":4)"));
    CHECK(holds(result.body, R"("data-cnt":5)"));
    CHECK(holds(result.body, R"("noans-cnt":6)"));
    CHECK(holds(result.body, R"("busy-cnt":7)"));
    CHECK(holds(result.body, R"("failed-cnt":9)"));
    CHECK(holds(result.body, R"("moc-dur":3314)"));
    CHECK(holds(result.body, R"("mtc-dur":120)"));
    CHECK(holds(result.body, R"("data-dur":60)"));
    CHECK(holds(result.body, R"("data-rx":8215)"));
    CHECK(holds(result.body, R"("data-tx":9273)"));
}

TEST_CASE("stats_service_reports_the_byte_totals_unscaled")
{
    FakeStore store;
    store.put(std::string(kTotalKey), kFieldDataRx, "2048");
    const StatsService service(store);

    CHECK(holds(service.totals().body, R"("data-rx":2048)"));
}

TEST_CASE("stats_service_answers_503_for_totals_the_store_cannot_be_read_for")
{
    FakeStore store;
    store.storeUp = false;
    const StatsService service(store);

    CHECK(service.totals().status == 503);
}
