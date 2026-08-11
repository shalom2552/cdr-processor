#include "doctest.h"
#include "constants.hpp"
#include "query/services/rank_service.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace cdrp;

/* A store answering out of a map of boards, failing every read when a test says so */
class FakeStore : public IQueryStore {
public:
    bool hgetall(const std::string_view, Fields& out) const override
    {
        out.clear();
        return storeUp;
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
        return storeUp;
    }

    bool top(std::string_view board, std::size_t offset, std::size_t limit,
             Ranked& out, uint64_t& count) const override
    {
        out.clear();
        count = 0;
        if (!storeUp) return false;

        asked.assign(board);
        const auto found = boards.find(std::string(board));
        if (found == boards.end()) {
            return true;
        }

        count = found->second.size();
        if (offset >= found->second.size()) {
            return true;
        }

        const std::size_t left = found->second.size() - offset;
        const std::size_t taken = limit == 0 ? left : std::min(limit, left);
        out.assign(found->second.begin() + static_cast<std::ptrdiff_t>(offset),
                   found->second.begin() + static_cast<std::ptrdiff_t>(offset + taken));
        return true;
    }

    /* Adds one member to one board, the board made when it is written to first */
    void put(std::string_view board, const std::string& member, uint64_t score)
    {
        boards[std::string(board)].emplace_back(member, score);
    }

    std::map<std::string, Ranked> boards;
    mutable std::string asked;
    bool storeUp = true;
};

/* A store holding three subscribers on the voice board, highest score first */
FakeStore seeded()
{
    FakeStore store;
    store.put(kVoiceBoard, "972500000001", 184920);
    store.put(kVoiceBoard, "972500000002", 9300);
    store.put(kVoiceBoard, "972500000003", 60);
    return store;
}

/* Parameters asking for one page */
QueryParams paged(std::size_t offset, std::size_t limit)
{
    QueryParams params;
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

TEST_CASE("rank_service_answers_a_board_with_its_entries")
{
    const FakeStore store = seeded();
    const RankService service(store);

    const Result result = service.top("voice", paged(0, 20));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("board":"voice")"));
    CHECK(holds(result.body, R"("count":3)"));
    CHECK(holds(result.body, R"("offset":0)"));
    CHECK(holds(result.body, R"("limit":20)"));
    CHECK(holds(result.body, R"({"id":"972500000001","score":184920})"));
}

TEST_CASE("rank_service_keeps_the_order_the_store_handed_it")
{
    const FakeStore store = seeded();
    const RankService service(store);

    const Result result = service.top("voice", paged(0, 20));

    CHECK(result.body.find("972500000001") < result.body.find("972500000002"));
    CHECK(result.body.find("972500000002") < result.body.find("972500000003"));
}

TEST_CASE("rank_service_maps_every_board_name_to_its_key")
{
    FakeStore store;
    const RankService service(store);
    const std::vector<std::pair<std::string, std::string_view>> boards = {
        { "voice",    kVoiceBoard   },
        { "sms",      kSmsBoard     },
        { "data",     kDataBoard    },
        { "fail",     kFailBoard    },
        { "op-voice", kOpVoiceBoard },
        { "op-sms",   kOpSmsBoard   },
    };

    for (const auto& [name, key] : boards) {
        CHECK(service.top(name, paged(0, 20)).status == 200);
        CHECK(store.asked == key);
    }
}

TEST_CASE("rank_service_answers_400_for_a_board_that_does_not_exist")
{
    const FakeStore store = seeded();
    const RankService service(store);

    const Result result = service.top("calls", paged(0, 20));

    CHECK(result.status == 400);
    CHECK_FALSE(result.body.empty());
}

TEST_CASE("rank_service_answers_200_and_no_entries_for_an_empty_board")
{
    FakeStore store;
    const RankService service(store);

    const Result result = service.top("sms", paged(0, 20));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("count":0)"));
    CHECK(holds(result.body, R"("entries":[])"));
}

TEST_CASE("rank_service_answers_the_page_that_was_asked_for")
{
    const FakeStore store = seeded();
    const RankService service(store);

    const Result result = service.top("voice", paged(1, 1));

    CHECK(result.status == 200);
    CHECK(holds(result.body, "972500000002"));
    CHECK_FALSE(holds(result.body, "972500000001"));
    CHECK_FALSE(holds(result.body, "972500000003"));
}

TEST_CASE("rank_service_answers_the_whole_board_count_for_a_page_of_it")
{
    const FakeStore store = seeded();
    const RankService service(store);

    CHECK(holds(service.top("voice", paged(1, 1)).body, R"("count":3)"));
}

TEST_CASE("rank_service_answers_no_entries_for_an_offset_past_the_end")
{
    const FakeStore store = seeded();
    const RankService service(store);

    const Result result = service.top("voice", paged(9, 20));

    CHECK(result.status == 200);
    CHECK(holds(result.body, R"("count":3)"));
    CHECK(holds(result.body, R"("entries":[])"));
}

TEST_CASE("rank_service_reports_the_limit_it_was_handed")
{
    const FakeStore store = seeded();
    const RankService service(store);

    CHECK(holds(service.top("voice", paged(0, kTopLimitMax)).body,
                R"("limit":)" + std::to_string(kTopLimitMax)));
}

TEST_CASE("rank_service_answers_503_when_the_store_cannot_be_read")
{
    FakeStore store = seeded();
    store.storeUp = false;
    const RankService service(store);

    CHECK(service.top("voice", paged(0, 20)).status == 503);
}
