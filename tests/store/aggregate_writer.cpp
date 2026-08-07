#include "doctest.h"
#include "constants.hpp"
#include "store/aggregate_writer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace cdrp;

/* One increment as the writer asked for it */
struct Call {
    std::string key;
    std::string field;
    uint64_t value = 0;
};

/* A store that remembers every call and fails the ones a test tells it to */
class FakeStore : public IStore {
public:
    bool increment(std::string_view key, std::string_view field, uint64_t value) override
    {
        calls.push_back(Call { std::string(key), std::string(field), value });
        return incrementOk;
    }

    bool flush() override
    {
        ++flushes;
        return flushOk;
    }

    /* The value written to key and field, or -1 when it was never written */
    long long valueOf(const std::string& key, const std::string& field) const
    {
        for (const Call& call : calls) {
            if (call.key == key && call.field == field) {
                return static_cast<long long>(call.value);
            }
        }
        return -1;
    }

    std::vector<Call> calls;
    std::size_t flushes = 0;
    bool incrementOk = true;
    bool flushOk = true;
};

const std::string kSubKey = std::string(kSubPrefix) + "972528409042";
const std::string kOpKey = std::string(kOpPrefix) + "42502";
const std::string kLinkKey = std::string(kLinkPrefix) + "972528409042";

} // namespace

using namespace cdrp;

TEST_CASE("aggregate_writer_writes_nothing_but_a_flush_for_an_empty_delta")
{
    FakeStore store;
    AggregateWriter writer(store);

    CHECK(writer.write(Delta {}));

    CHECK(store.calls.empty());
    CHECK(store.flushes == 1);
}

TEST_CASE("aggregate_writer_skips_the_zero_counters_of_a_subscriber")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 1);
    CHECK(store.valueOf(kSubKey, std::string(kFieldVoiceOut)) == 60);
}

TEST_CASE("aggregate_writer_writes_every_subscriber_counter")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL] = SubDelta { 60, 40, 8215, 9273, 3, 2, 1, 1, 1 };

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 9);
    CHECK(store.valueOf(kSubKey, std::string(kFieldVoiceOut)) == 60);
    CHECK(store.valueOf(kSubKey, std::string(kFieldVoiceIn)) == 40);
    CHECK(store.valueOf(kSubKey, std::string(kFieldDataRx)) == 8215);
    CHECK(store.valueOf(kSubKey, std::string(kFieldDataTx)) == 9273);
    CHECK(store.valueOf(kSubKey, std::string(kFieldSmsOut)) == 3);
    CHECK(store.valueOf(kSubKey, std::string(kFieldSmsIn)) == 2);
    CHECK(store.valueOf(kSubKey, std::string(kFieldNoans)) == 1);
    CHECK(store.valueOf(kSubKey, std::string(kFieldBusy)) == 1);
    CHECK(store.valueOf(kSubKey, std::string(kFieldFailed)) == 1);
    CHECK(store.flushes == 1);
}

TEST_CASE("aggregate_writer_writes_the_operator_counters")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.ops[42502].voice_out = 60;
    delta.ops[42502].voice_in = 40;
    delta.ops[42502].sms_out = 3;
    delta.ops[42502].sms_in = 2;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 4);
    CHECK(store.valueOf(kOpKey, std::string(kFieldVoiceOut)) == 60);
    CHECK(store.valueOf(kOpKey, std::string(kFieldVoiceIn)) == 40);
    CHECK(store.valueOf(kOpKey, std::string(kFieldSmsOut)) == 3);
    CHECK(store.valueOf(kOpKey, std::string(kFieldSmsIn)) == 2);
}

TEST_CASE("aggregate_writer_writes_a_link_under_the_owner_and_the_peer")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.links[LinkKey { 972528409042ULL, 496221540ULL }].dur = 15;
    delta.links[LinkKey { 972528409042ULL, 496221540ULL }].sms = 2;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.valueOf(kLinkKey, "496221540" + std::string(kFieldDurSuffix)) == 15);
    CHECK(store.valueOf(kLinkKey, "496221540" + std::string(kFieldSmsSuffix)) == 2);
}

TEST_CASE("aggregate_writer_keeps_the_two_directions_of_a_pair_apart")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.links[LinkKey { 972528409042ULL, 496221540ULL }].dur = 15;
    delta.links[LinkKey { 496221540ULL, 972528409042ULL }].dur = 15;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.valueOf(kLinkKey, "496221540" + std::string(kFieldDurSuffix)) == 15);
    CHECK(store.valueOf(std::string(kLinkPrefix) + "496221540",
              "972528409042" + std::string(kFieldDurSuffix))
        == 15);
}

TEST_CASE("aggregate_writer_writes_every_map_of_one_delta")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].sms_out = 1;
    delta.ops[42502].sms_out = 1;
    delta.links[LinkKey { 972528409042ULL, 496221540ULL }].sms = 1;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 3);
    CHECK(store.valueOf(kSubKey, std::string(kFieldSmsOut)) == 1);
    CHECK(store.valueOf(kOpKey, std::string(kFieldSmsOut)) == 1);
    CHECK(store.valueOf(kLinkKey, "496221540" + std::string(kFieldSmsSuffix)) == 1);
    CHECK(store.flushes == 1);
}

TEST_CASE("aggregate_writer_keeps_subscribers_apart")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 3;
    delta.subs[972528409999ULL].voice_out = 7;

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.valueOf(kSubKey, std::string(kFieldVoiceOut)) == 3);
    CHECK(store.valueOf(std::string(kSubPrefix) + "972528409999",
              std::string(kFieldVoiceOut))
        == 7);
}

TEST_CASE("aggregate_writer_writes_a_counter_larger_than_a_32_bit_total")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].data_rx = 8589934592ULL;

    CHECK(writer.write(delta));

    CHECK(store.valueOf(kSubKey, std::string(kFieldDataRx)) == 8589934592LL);
}

TEST_CASE("aggregate_writer_fails_when_the_store_refuses_a_counter")
{
    FakeStore store;
    store.incrementOk = false;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK_FALSE(writer.write(delta));
}

TEST_CASE("aggregate_writer_fails_when_the_flush_fails")
{
    FakeStore store;
    store.flushOk = false;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK_FALSE(writer.write(delta));
    CHECK(store.flushes == 1);
}

TEST_CASE("aggregate_writer_writes_a_full_batch_of_subscribers")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    for (uint64_t index = 0; index < 1000; ++index) {
        delta.subs[972528409042ULL + index].voice_out = 1;
    }

    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 1000);
    CHECK(store.flushes == 1);
}

TEST_CASE("aggregate_writer_can_be_used_through_a_second_write")
{
    FakeStore store;
    AggregateWriter writer(store);
    Delta delta;
    delta.subs[972528409042ULL].voice_out = 60;

    CHECK(writer.write(delta));
    CHECK(writer.write(delta));

    CHECK(store.calls.size() == 2);
    CHECK(store.flushes == 2);
}
