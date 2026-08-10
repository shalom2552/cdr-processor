#include "doctest.h"
#include "config.hpp"
#include "constants.hpp"
#include "query/services/path_service.hpp"

#include <chrono>
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

    bool hkeys(const std::string_view key, std::vector<std::string>& out) const override
    {
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
};

/* A store holding the chain first - second - third */
FakeStore seeded()
{
    FakeStore store;
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

TEST_CASE("path_service_finds_the_path_of_a_pair_in_contact")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kSecond, false);

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
    CHECK(holds(result.body, kSecond));
}

TEST_CASE("path_service_finds_the_path_over_one_subscriber_between")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kThird, false);

    CHECK(result.status == 200);
    CHECK(result.body.find(kFirst) < result.body.find(kSecond));
    CHECK(result.body.find(kSecond) < result.body.find(kThird));
}

TEST_CASE("path_service_finds_the_path_of_a_subscriber_to_itself")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kFirst, false);

    CHECK(result.status == 200);
    CHECK(holds(result.body, kFirst));
}

TEST_CASE("path_service_reports_no_hops_when_none_were_asked_for")
{
    const FakeStore store = seeded();
    const PathService service(store);

    CHECK_FALSE(holds(service.path(kFirst, kThird, false).body, "hops"));
}

TEST_CASE("path_service_reports_what_one_hop_carried")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kSecond, true);

    CHECK(result.status == 200);
    CHECK(holds(result.body,
        R"("hops":[{"from":"972500000001","to":"972500000002","duration":60,"sms":3}])"));
}

TEST_CASE("path_service_reports_what_every_hop_of_two_carried")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kThird, true);

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"({"from":"972500000001","to":"972500000002","duration":60,"sms":3})"));
    CHECK(holds(result.body, R"({"from":"972500000002","to":"972500000003","duration":90,"sms":5})"));
}

TEST_CASE("path_service_reports_no_hops_for_a_path_of_one_subscriber")
{
    const FakeStore store = seeded();
    const PathService service(store);

    CHECK(holds(service.path(kFirst, kFirst, true).body, R"("hops":[])"));
}

TEST_CASE("path_service_answers_404_for_a_path_that_is_not_there")
{
    const FakeStore store = seeded();
    const PathService service(store);

    CHECK(service.path(kFirst, kStranger, false).status == 404);
}

TEST_CASE("path_service_reports_the_bounds_it_gave_up_at")
{
    const FakeStore store = seeded();
    const PathService service(store);

    const Result result = service.path(kFirst, kStranger, false);

    CHECK(holds(result.body, R"("max-hops":)" + std::to_string(cfg.query.max_hops)));
    CHECK(holds(result.body, R"("max-visited":)" + std::to_string(cfg.query.max_visited)));
}

TEST_CASE("path_service_answers_404_for_a_path_longer_than_the_hop_limit")
{
    FakeStore store = seeded();
    std::vector<std::string> chain;
    for (std::size_t hop = 0; hop <= cfg.query.max_hops + 4; ++hop) {
        chain.push_back("9725100000" + std::to_string(hop));
    }
    for (std::size_t hop = 1; hop < chain.size(); ++hop) {
        store.link(chain[hop - 1], chain[hop], "10", "1");
    }
    const PathService service(store);

    CHECK(service.path(chain.front(), chain.back(), false).status == 404);
}

TEST_CASE("path_service_answers_503_for_a_path_the_store_cannot_be_read_for")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const PathService service(store);

    CHECK(service.path(kFirst, kThird, false).status == 503);
}

TEST_CASE("path_service_gives_up_on_a_wide_graph_instead_of_searching_it_whole")
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
    const PathService service(store);
    Result result;

    const long long elapsed = millisOf([&] { result = service.path(kFirst, kStranger, false); });

    CHECK(result.status == 404);
    CHECK(elapsed < 10000);
}
