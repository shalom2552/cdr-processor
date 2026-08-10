#include "doctest.h"
#include "config.hpp"
#include "constants.hpp"
#include "sink/aggregate_sink.hpp"
#include "store/redis_store.hpp"

#include <hiredis/hiredis.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace cdrp;

/* Keys of this suite's own, so a live server keeps none of it that matters */
constexpr uint64_t kImsi = 999990000000001ULL; // MCCMNC 99999
constexpr uint32_t kOpCode = 99999;
constexpr uint64_t kMsisdn = 999999999999ULL;
constexpr uint64_t kPeerMsisdn = 999999999998ULL;

const std::string kSubKey = std::string(kSubPrefix) + std::to_string(kMsisdn);
const std::string kPeerSubKey = std::string(kSubPrefix) + std::to_string(kPeerMsisdn);
const std::string kOpKey = std::string(kOpPrefix) + std::to_string(kOpCode);
const std::string kLinkKey = std::string(kLinkPrefix) + std::to_string(kMsisdn);
const std::string kPeerLinkKey = std::string(kLinkPrefix) + std::to_string(kPeerMsisdn);

/* A connection of this suite's own, null when the server is not there */
redisContext* probe()
{
    const timeval timeout { 1, 0 };
    redisContext* ctx
        = redisConnectWithTimeout(cfg.redis.host.c_str(), cfg.redis.port, timeout);
    if (ctx && ctx->err) {
        redisFree(ctx);
        return nullptr;
    }
    return ctx;
}

/* Drops every key this suite wrote, so a live server is left as it was found */
struct Cleanup {
    ~Cleanup()
    {
        redisContext* ctx = probe();
        if (!ctx) {
            return;
        }
        for (const std::string* key :
            { &kSubKey, &kPeerSubKey, &kOpKey, &kLinkKey, &kPeerLinkKey }) {
            freeReplyObject(redisCommand(ctx, "DEL %b", key->data(), key->size()));
        }
        redisFree(ctx);
    }
};

const Cleanup cleanup;

/* True when the configured server answered one write, asked once for the whole suite */
bool serverUp()
{
    static const bool up = [] {
        RedisStore store;
        return store.increment(kSubKey, "probe", 1) && store.flush();
    }();
    return up;
}

/* The counter held by key and field, 0 when it was never written */
long long counterOf(const std::string& key, const std::string& field)
{
    redisContext* ctx = probe();
    if (!ctx) {
        return 0;
    }
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "HGET %b %b", key.data(), key.size(), field.data(), field.size()));
    long long value = 0;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        value = std::stoll(std::string(reply->str, reply->len));
    }
    freeReplyObject(reply);
    redisFree(ctx);
    return value;
}

/* One record of the given type, subscriber and second party filled in */
CdrRecord makeRecord(UsageType type, uint64_t duration = 0)
{
    CdrRecord record {};
    record.sequence = 1;
    record.subscriberImsi = kImsi;
    record.subscriberImei = "35-209900-176148-1";
    record.usageType = type;
    record.subscriberMSISDN = kMsisdn;
    record.callTime = 0;
    record.duration = duration;
    record.secondPartyIMSI = kImsi;
    record.secondPartyMSISDN = kPeerMsisdn;
    return record;
}

/* A store that remembers what it was asked for, in the order it was asked */
class FakeStore : public IStore {
public:
    bool increment(std::string_view, std::string_view, uint64_t) override
    {
        calls.emplace_back("increment");
        return true;
    }

    bool flush() override
    {
        calls.emplace_back("flush");
        return true;
    }

    uint64_t resume_at(std::string_view source) override
    {
        asked.assign(source);
        return resume;
    }

    bool mark(std::string_view source, uint64_t seq) override
    {
        calls.emplace_back("mark");
        marked.assign(source);
        markedSeq = seq;
        ++marks;
        return true;
    }

    /* Index of the first call by that name, or -1 when it was never made */
    long long indexOf(const std::string& name) const
    {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (calls[i] == name) {
                return static_cast<long long>(i);
            }
        }
        return -1;
    }

    std::vector<std::string> calls;
    std::string asked;
    std::string marked;
    uint64_t markedSeq = 0;
    std::size_t marks = 0;
    uint64_t resume = 0;
};

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

TEST_CASE("aggregate_sink_is_a_sink")
{
    CHECK(std::is_base_of<ISink, AggregateSink>::value);
    CHECK(std::has_virtual_destructor<ISink>::value);
}

TEST_CASE("aggregate_sink_refuses_a_null_store")
{
    CHECK_THROWS_AS(AggregateSink(nullptr), std::invalid_argument);
}

TEST_CASE("aggregate_sink_takes_an_empty_batch")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch;

    const long long elapsed = millisOf([&] { sink.consume(batch, ""); });

    CHECK(elapsed < 5000);
}

TEST_CASE("aggregate_sink_starts_with_a_total_of_zero")
{
    const AggregateSink sink(std::make_unique<RedisStore>());

    CHECK(sink.snapshot().records == 0);
}

TEST_CASE("aggregate_sink_counts_the_records_of_every_batch_it_took")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch(3, makeRecord(UsageType::MOC, 60));
    std::vector<CdrRecord> empty;

    sink.consume(batch, "");
    sink.consume(empty, "");
    sink.consume(batch, "");

    CHECK(sink.snapshot().records == 6);
    CHECK(sink.snapshot().moc_cnt == 6);
    CHECK(sink.snapshot().moc_dur == 360);
}

TEST_CASE("aggregate_sink_counts_a_record_that_fell_nowhere")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 60) };
    batch[0].subscriberMSISDN = 0;

    sink.consume(batch, "");

    CHECK(sink.snapshot().records == 1);
    CHECK(sink.snapshot().moc_cnt == 1);
}

TEST_CASE("aggregate_sink_counts_every_thread_into_one_total")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<std::thread> threads;
    const std::size_t rounds = 16;

    for (std::size_t index = 0; index < 4; ++index) {
        threads.emplace_back([&] {
            for (std::size_t round = 0; round < rounds; ++round) {
                std::vector<CdrRecord> batch(2, makeRecord(UsageType::SMS_MO));
                sink.consume(batch, "");
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    CHECK(sink.snapshot().records == 2 * 4 * rounds);
    CHECK(sink.snapshot().sms_mo_cnt == 2 * 4 * rounds);
}

TEST_CASE("aggregate_sink_takes_a_batch_of_records_without_a_subscriber")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch(4, makeRecord(UsageType::MOC, 60));
    for (CdrRecord& record : batch) {
        record.subscriberMSISDN = 0;
    }

    const long long elapsed = millisOf([&] { sink.consume(batch, ""); });

    CHECK(elapsed < 5000);
}

TEST_CASE("aggregate_sink_adds_the_seconds_of_a_call_to_the_subscriber")
{
    if (!serverUp()) {
        return;
    }
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 60) };
    const long long before = counterOf(kSubKey, std::string(kFieldVoiceOut));

    sink.consume(batch, "");

    CHECK(counterOf(kSubKey, std::string(kFieldVoiceOut)) == before + 60);
}

TEST_CASE("aggregate_sink_adds_the_operator_counters_of_a_batch")
{
    if (!serverUp()) {
        return;
    }
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::SMS_MO) };
    const long long before = counterOf(kOpKey, std::string(kFieldSmsOut));

    sink.consume(batch, "");

    CHECK(counterOf(kOpKey, std::string(kFieldSmsOut)) == before + 1);
}

TEST_CASE("aggregate_sink_adds_a_link_under_both_parties")
{
    if (!serverUp()) {
        return;
    }
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 15) };
    const std::string ownerField = std::to_string(kPeerMsisdn) + std::string(kFieldDurSuffix);
    const std::string peerField = std::to_string(kMsisdn) + std::string(kFieldDurSuffix);
    const long long ownerBefore = counterOf(kLinkKey, ownerField);
    const long long peerBefore = counterOf(kPeerLinkKey, peerField);

    sink.consume(batch, "");

    CHECK(counterOf(kLinkKey, ownerField) == ownerBefore + 15);
    CHECK(counterOf(kPeerLinkKey, peerField) == peerBefore + 15);
}

TEST_CASE("aggregate_sink_adds_a_counter_larger_than_a_32_bit_total")
{
    if (!serverUp()) {
        return;
    }
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::D) };
    batch[0].bytesReceived = 8589934592ULL;
    const long long before = counterOf(kSubKey, std::string(kFieldDataRx));

    sink.consume(batch, "");

    CHECK(counterOf(kSubKey, std::string(kFieldDataRx)) == before + 8589934592LL);
}

TEST_CASE("aggregate_sink_adds_every_record_of_a_full_batch")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch(kBatchSize, makeRecord(UsageType::MOC, 1));
    const long long before
        = serverUp() ? counterOf(kSubKey, std::string(kFieldVoiceOut)) : 0;

    const long long elapsed = millisOf([&] { sink.consume(batch, ""); });

    CHECK(elapsed < 30000);
    if (serverUp()) {
        CHECK(counterOf(kSubKey, std::string(kFieldVoiceOut))
            == before + static_cast<long long>(kBatchSize));
    }
}

TEST_CASE("aggregate_sink_consumes_two_batches_in_a_row")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    std::vector<CdrRecord> batch { makeRecord(UsageType::SMS_MO) };
    const long long before = serverUp() ? counterOf(kSubKey, std::string(kFieldSmsOut)) : 0;

    sink.consume(batch, "");
    std::vector<CdrRecord> second { makeRecord(UsageType::SMS_MO) };
    sink.consume(second, "");

    if (serverUp()) {
        CHECK(counterOf(kSubKey, std::string(kFieldSmsOut)) == before + 2);
    }
}

TEST_CASE("aggregate_sink_consumes_through_the_sink_interface")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    ISink& isink = sink;
    std::vector<CdrRecord> batch { makeRecord(UsageType::MTC, 30) };
    const long long before = serverUp() ? counterOf(kSubKey, std::string(kFieldVoiceIn)) : 0;

    const long long elapsed = millisOf([&] { isink.consume(batch, ""); });

    CHECK(elapsed < 5000);
    if (serverUp()) {
        CHECK(counterOf(kSubKey, std::string(kFieldVoiceIn)) == before + 30);
    }
}

TEST_CASE("aggregate_sink_marks_the_highest_sequence_of_a_named_source")
{
    auto store = std::make_unique<FakeStore>();
    FakeStore& fake = *store;
    AggregateSink sink(std::move(store));
    std::vector<CdrRecord> batch(3, makeRecord(UsageType::MOC, 60));
    batch[1].sequence = 42;
    batch[2].sequence = 7;

    sink.consume(batch, "run.cdr");

    CHECK(fake.marks == 1);
    CHECK(fake.marked == "run.cdr");
    CHECK(fake.markedSeq == 42);
}

TEST_CASE("aggregate_sink_marks_a_source_before_the_flush_that_commits_it")
{
    auto store = std::make_unique<FakeStore>();
    FakeStore& fake = *store;
    AggregateSink sink(std::move(store));
    std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 60) };

    sink.consume(batch, "run.cdr");

    const long long mark = fake.indexOf("mark");
    const long long flush = fake.indexOf("flush");
    REQUIRE(mark >= 0);
    REQUIRE(flush >= 0);
    CHECK(mark < flush);
    CHECK(fake.indexOf("increment") < mark);
}

TEST_CASE("aggregate_sink_marks_nothing_for_a_source_it_was_not_given")
{
    auto store = std::make_unique<FakeStore>();
    FakeStore& fake = *store;
    AggregateSink sink(std::move(store));
    std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 60) };

    sink.consume(batch, "");

    CHECK(fake.marks == 0);
}

TEST_CASE("aggregate_sink_marks_nothing_for_an_empty_batch")
{
    auto store = std::make_unique<FakeStore>();
    FakeStore& fake = *store;
    AggregateSink sink(std::move(store));
    std::vector<CdrRecord> batch;

    sink.consume(batch, "run.cdr");

    CHECK(fake.marks == 0);
}

TEST_CASE("aggregate_sink_asks_the_store_where_a_source_resumes")
{
    auto store = std::make_unique<FakeStore>();
    FakeStore& fake = *store;
    fake.resume = 17;
    AggregateSink sink(std::move(store));

    CHECK(sink.resume_at("run.cdr") == 17);
    CHECK(fake.asked == "run.cdr");
}

TEST_CASE("aggregate_sink_consumes_from_several_threads_at_once")
{
    AggregateSink sink(std::make_unique<RedisStore>());
    const long long before = serverUp() ? counterOf(kSubKey, std::string(kFieldNoans)) : 0;
    std::vector<std::thread> threads;
    const std::size_t rounds = 16;

    const long long elapsed = millisOf([&] {
        for (std::size_t index = 0; index < 4; ++index) {
            threads.emplace_back([&] {
                for (std::size_t round = 0; round < rounds; ++round) {
                    std::vector<CdrRecord> batch { makeRecord(UsageType::U) };
                    sink.consume(batch, "");
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    });

    CHECK(elapsed < 30000);
    if (serverUp()) {
        CHECK(counterOf(kSubKey, std::string(kFieldNoans))
            == before + static_cast<long long>(4 * rounds));
    }
}
