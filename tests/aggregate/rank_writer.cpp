#include "doctest.h"
#include "constants.hpp"
#include "aggregate/rank_writer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace cdrp;

/* One score as the writer asked for it */
struct Call {
    std::string board;
    std::string member;
    uint64_t value = 0;
};

/* A store that remembers every board call and fails the ones a test tells it to */
class FakeStore : public IStore {
public:
    bool increment(std::string_view, std::string_view, uint64_t) override
    {
        return true;
    }

    bool rank(std::string_view board, std::string_view member, uint64_t value) override
    {
        calls.push_back(Call { std::string(board), std::string(member), value });
        return rankOk;
    }

    bool flush() override
    {
        ++flushes;
        return true;
    }

    uint64_t resume_at(std::string_view) override
    {
        return 0;
    }

    bool mark(std::string_view, uint64_t) override
    {
        return true;
    }

    /* The score written to board for member, or -1 when it was never written */
    long long valueOf(std::string_view board, const std::string& member) const
    {
        for (const Call& call : calls) {
            if (call.board == board && call.member == member) {
                return static_cast<long long>(call.value);
            }
        }
        return -1;
    }

    std::vector<Call> calls;
    std::size_t flushes = 0;
    bool rankOk = true;
};

const std::string kMsisdn = "972528409042";
const std::string kMccmnc = "42502";

} // namespace

using namespace cdrp;

TEST_CASE("rank_writer_writes_nothing_for_an_empty_delta")
{
    FakeStore store;
    RankWriter writer(store);

    CHECK(writer.write(Delta {}));

    CHECK(store.calls.empty());
}

TEST_CASE("rank_writer_does_not_flush_what_it_queued")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK(writer.write(delta));

    CHECK(store.flushes == 0);
}

TEST_CASE("rank_writer_ranks_a_subscriber_on_every_board")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL] = SubDelta { 60, 40, 8215, 9273, 3, 2, 1, 1, 1 };

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 4);
    CHECK(store.valueOf(kVoiceBoard, kMsisdn) == 100);
    CHECK(store.valueOf(kSmsBoard, kMsisdn) == 5);
    CHECK(store.valueOf(kDataBoard, kMsisdn) == 17488);
    CHECK(store.valueOf(kFailBoard, kMsisdn) == 3);
}

TEST_CASE("rank_writer_skips_the_boards_a_subscriber_scores_nothing_on")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].sms_out = 3;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 1);
    CHECK(store.valueOf(kSmsBoard, kMsisdn) == 3);
    CHECK(store.valueOf(kVoiceBoard, kMsisdn) == -1);
}

TEST_CASE("rank_writer_ranks_an_operator_on_its_own_boards")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.ops[42502] = OpDelta { 600, 400, 30, 20 };

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.valueOf(kOpVoiceBoard, kMccmnc) == 1000);
    CHECK(store.valueOf(kOpSmsBoard, kMccmnc) == 50);
    CHECK(store.valueOf(kVoiceBoard, kMccmnc) == -1);
}

TEST_CASE("rank_writer_keeps_subscribers_apart")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 3;
    delta.subs[972528409999ULL].voice_out = 7;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.valueOf(kVoiceBoard, kMsisdn) == 3);
    CHECK(store.valueOf(kVoiceBoard, "972528409999") == 7);
}

TEST_CASE("rank_writer_ranks_nothing_for_the_links_of_a_delta")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.links[LinkKey { 972528409042ULL, 496221540ULL }].dur = 15;

    CHECK(writer.write(delta));

    CHECK(store.calls.empty());
}

TEST_CASE("rank_writer_writes_a_score_larger_than_a_32_bit_counter")
{
    FakeStore store;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].data_rx = 8589934592ULL;

    CHECK(writer.write(delta));

    CHECK(store.valueOf(kDataBoard, kMsisdn) == 8589934592LL);
}

TEST_CASE("rank_writer_fails_when_the_store_refuses_a_score")
{
    FakeStore store;
    store.rankOk = false;
    RankWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK_FALSE(writer.write(delta));
}
