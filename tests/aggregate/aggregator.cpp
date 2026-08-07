#include "doctest.h"
#include "aggregate/aggregator.hpp"
#include "constants.hpp"

#include <cstdint>
#include <vector>

namespace {

using namespace cdrp;

constexpr uint64_t kImsi = 425020528409010ULL;  // MCCMNC 42502
constexpr uint64_t kOtherImsi = 262040162782277ULL; // MCCMNC 26204
constexpr uint64_t kMsisdn = 972528409042ULL;
constexpr uint64_t kPeerMsisdn = 496221540ULL;

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
    record.secondPartyIMSI = kOtherImsi;
    record.secondPartyMSISDN = kPeerMsisdn;
    return record;
}

/* The subscriber bucket of key, or a zeroed one when the batch never made it */
SubDelta sub(const Delta& delta, uint64_t key)
{
    const auto found = delta.subs.find(key);
    return (found == delta.subs.end()) ? SubDelta {} : found->second;
}

/* The operator bucket of key, or a zeroed one when the batch never made it */
OpDelta op(const Delta& delta, uint32_t key)
{
    const auto found = delta.ops.find(key);
    return (found == delta.ops.end()) ? OpDelta {} : found->second;
}

/* The link bucket of owner to peer, or a zeroed one when the batch never made it */
LinkDelta link(const Delta& delta, uint64_t owner, uint64_t peer)
{
    const auto found = delta.links.find(LinkKey { owner, peer });
    return (found == delta.links.end()) ? LinkDelta {} : found->second;
}

} // namespace

using namespace cdrp;

TEST_CASE("fold_leaves_an_empty_batch_empty")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({}, delta);

    CHECK(delta.subs.empty());
    CHECK(delta.ops.empty());
    CHECK(delta.links.empty());
}

TEST_CASE("fold_clears_the_delta_it_is_given")
{
    const Aggregator aggregator;
    Delta delta;
    delta.subs[7].voice_out = 99;
    delta.ops[7].sms_in = 99;
    delta.links[LinkKey { 7, 8 }].dur = 99;

    aggregator.fold({}, delta);

    CHECK(delta.subs.empty());
    CHECK(delta.ops.empty());
    CHECK(delta.links.empty());
}

TEST_CASE("fold_counts_an_outgoing_call_for_the_caller")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 3314) }, delta);

    CHECK(sub(delta, kMsisdn).voice_out == 3314);
    CHECK(sub(delta, kMsisdn).voice_in == 0);
    CHECK(sub(delta, kMsisdn).sms_out == 0);
    CHECK(sub(delta, kMsisdn).data_rx == 0);
}

TEST_CASE("fold_counts_an_incoming_call_for_the_callee")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MTC, 120) }, delta);

    CHECK(sub(delta, kMsisdn).voice_in == 120);
    CHECK(sub(delta, kMsisdn).voice_out == 0);
}

TEST_CASE("fold_counts_one_message_each_way")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::SMS_MO), makeRecord(UsageType::SMS_MT) }, delta);

    CHECK(sub(delta, kMsisdn).sms_out == 1);
    CHECK(sub(delta, kMsisdn).sms_in == 1);
    CHECK(sub(delta, kMsisdn).voice_out == 0);
    CHECK(sub(delta, kMsisdn).voice_in == 0);
}

TEST_CASE("fold_counts_data_bytes_each_way")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord record = makeRecord(UsageType::D, 120);
    record.bytesReceived = 8215;
    record.bytesTransmitted = 9273;

    aggregator.fold({ record }, delta);

    CHECK(sub(delta, kMsisdn).data_rx == 8215);
    CHECK(sub(delta, kMsisdn).data_tx == 9273);
    CHECK(sub(delta, kMsisdn).voice_out == 0);
}

TEST_CASE("fold_counts_unanswered_busy_and_failed_calls_apart")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::U), makeRecord(UsageType::B),
                        makeRecord(UsageType::X) },
        delta);

    CHECK(sub(delta, kMsisdn).noans == 1);
    CHECK(sub(delta, kMsisdn).busy == 1);
    CHECK(sub(delta, kMsisdn).failed == 1);
    CHECK(sub(delta, kMsisdn).voice_out == 0);
    CHECK(sub(delta, kMsisdn).voice_in == 0);
}

TEST_CASE("fold_leaves_unsuccessful_calls_out_of_the_operator_counters")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::U), makeRecord(UsageType::B),
                        makeRecord(UsageType::X) },
        delta);

    const OpDelta counters = op(delta, kImsi / kMsinDivisor);
    CHECK(counters.voice_out == 0);
    CHECK(counters.voice_in == 0);
    CHECK(counters.sms_out == 0);
    CHECK(counters.sms_in == 0);
}

TEST_CASE("fold_adds_every_record_of_one_subscriber_together")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 10), makeRecord(UsageType::MOC, 30),
                        makeRecord(UsageType::MTC, 5), makeRecord(UsageType::SMS_MO),
                        makeRecord(UsageType::SMS_MO) },
        delta);

    CHECK(delta.subs.size() == 1);
    CHECK(sub(delta, kMsisdn).voice_out == 40);
    CHECK(sub(delta, kMsisdn).voice_in == 5);
    CHECK(sub(delta, kMsisdn).sms_out == 2);
}

TEST_CASE("fold_keeps_subscribers_apart")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord other = makeRecord(UsageType::MOC, 7);
    other.subscriberMSISDN = 972528409999ULL;

    aggregator.fold({ makeRecord(UsageType::MOC, 3), other }, delta);

    CHECK(delta.subs.size() == 2);
    CHECK(sub(delta, kMsisdn).voice_out == 3);
    CHECK(sub(delta, 972528409999ULL).voice_out == 7);
}

TEST_CASE("fold_keys_operators_by_the_subscribers_own_imsi")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 60) }, delta);

    REQUIRE(delta.ops.size() == 1);
    CHECK(delta.ops.begin()->first == 42502);
    CHECK(op(delta, 42502).voice_out == 60);
}

TEST_CASE("fold_counts_operator_voice_and_sms_each_way")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 60), makeRecord(UsageType::MTC, 40),
                        makeRecord(UsageType::SMS_MO), makeRecord(UsageType::SMS_MT) },
        delta);

    const OpDelta counters = op(delta, 42502);
    CHECK(counters.voice_out == 60);
    CHECK(counters.voice_in == 40);
    CHECK(counters.sms_out == 1);
    CHECK(counters.sms_in == 1);
}

TEST_CASE("fold_keeps_operators_apart")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord other = makeRecord(UsageType::MOC, 7);
    other.subscriberImsi = kOtherImsi;
    other.subscriberMSISDN = 972528409999ULL;

    aggregator.fold({ makeRecord(UsageType::MOC, 3), other }, delta);

    CHECK(delta.ops.size() == 2);
    CHECK(op(delta, 42502).voice_out == 3);
    CHECK(op(delta, 26204).voice_out == 7);
}

TEST_CASE("fold_leaves_data_out_of_the_operator_counters")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord record = makeRecord(UsageType::D, 120);
    record.bytesReceived = 8215;
    record.bytesTransmitted = 9273;

    aggregator.fold({ record }, delta);

    const OpDelta counters = op(delta, 42502);
    CHECK(counters.voice_out == 0);
    CHECK(counters.voice_in == 0);
    CHECK(counters.sms_out == 0);
    CHECK(counters.sms_in == 0);
}

TEST_CASE("fold_links_a_call_to_the_second_party")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 3314) }, delta);

    CHECK(link(delta, kMsisdn, kPeerMsisdn).dur == 3314);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).sms == 0);
}

TEST_CASE("fold_links_a_message_to_the_second_party")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::SMS_MO) }, delta);

    CHECK(link(delta, kMsisdn, kPeerMsisdn).sms == 1);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).dur == 0);
}

TEST_CASE("fold_mirrors_a_link_onto_the_second_party")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 3314) }, delta);

    CHECK(delta.links.size() == 2);
    CHECK(link(delta, kPeerMsisdn, kMsisdn).dur == 3314);
    CHECK(delta.subs.count(kPeerMsisdn) == 0);
}

TEST_CASE("fold_keeps_the_two_directions_of_a_pair_apart")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord third = makeRecord(UsageType::MOC, 20);
    third.secondPartyMSISDN = 972528409999ULL;

    aggregator.fold({ makeRecord(UsageType::MOC, 10), third }, delta);

    CHECK(delta.links.size() == 4);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).dur == 10);
    CHECK(link(delta, kMsisdn, 972528409999ULL).dur == 20);
    CHECK(link(delta, kPeerMsisdn, 972528409999ULL).dur == 0);
}

TEST_CASE("fold_leaves_a_record_without_a_second_party_unlinked")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord record = makeRecord(UsageType::MOC, 30);
    record.secondPartyMSISDN = 0;
    record.secondPartyIMSI = 0;

    aggregator.fold({ record }, delta);

    CHECK(delta.links.empty());
    CHECK(sub(delta, kMsisdn).voice_out == 30);
}

TEST_CASE("fold_leaves_data_sessions_unlinked")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord record = makeRecord(UsageType::D, 120);
    record.bytesReceived = 8215;

    aggregator.fold({ record }, delta);

    CHECK(delta.links.empty());
}

TEST_CASE("fold_adds_repeated_contacts_into_one_link")
{
    const Aggregator aggregator;
    Delta delta;

    aggregator.fold({ makeRecord(UsageType::MOC, 10), makeRecord(UsageType::MTC, 5),
                        makeRecord(UsageType::SMS_MO), makeRecord(UsageType::SMS_MT) },
        delta);

    CHECK(delta.links.size() == 2);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).dur == 15);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).sms == 2);
    CHECK(link(delta, kPeerMsisdn, kMsisdn).dur == 15);
    CHECK(link(delta, kPeerMsisdn, kMsisdn).sms == 2);
}

TEST_CASE("fold_gives_the_same_answer_for_the_same_batch")
{
    const Aggregator aggregator;
    const std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 10),
        makeRecord(UsageType::SMS_MT), makeRecord(UsageType::B) };
    Delta first;
    Delta second;

    aggregator.fold(batch, first);
    aggregator.fold({ makeRecord(UsageType::MTC, 900) }, second);
    aggregator.fold(batch, second);

    CHECK(first.subs.size() == second.subs.size());
    CHECK(sub(first, kMsisdn).voice_out == sub(second, kMsisdn).voice_out);
    CHECK(sub(first, kMsisdn).voice_in == sub(second, kMsisdn).voice_in);
    CHECK(sub(first, kMsisdn).sms_in == sub(second, kMsisdn).sms_in);
    CHECK(sub(first, kMsisdn).busy == sub(second, kMsisdn).busy);
    CHECK(link(first, kMsisdn, kPeerMsisdn).dur == link(second, kMsisdn, kPeerMsisdn).dur);
}

TEST_CASE("fold_keeps_the_buckets_of_the_delta_it_reuses")
{
    const Aggregator aggregator;
    Delta delta;
    std::vector<CdrRecord> batch;
    for (uint64_t index = 0; index < 1000; ++index) {
        CdrRecord record = makeRecord(UsageType::MOC, 1);
        record.subscriberMSISDN = kMsisdn + index;
        batch.push_back(record);
    }

    aggregator.fold(batch, delta);
    const size_t buckets = delta.subs.bucket_count();
    aggregator.fold({ makeRecord(UsageType::MOC, 1) }, delta);

    CHECK(delta.subs.size() == 1);
    CHECK(delta.subs.bucket_count() == buckets);
}

TEST_CASE("fold_takes_a_full_batch")
{
    const Aggregator aggregator;
    Delta delta;
    std::vector<CdrRecord> batch;
    for (size_t index = 0; index < kBatchSize; ++index) {
        batch.push_back(makeRecord(UsageType::MOC, 1));
    }

    aggregator.fold(batch, delta);

    CHECK(delta.subs.size() == 1);
    CHECK(sub(delta, kMsisdn).voice_out == kBatchSize);
    CHECK(link(delta, kMsisdn, kPeerMsisdn).dur == kBatchSize);
}

TEST_CASE("fold_adds_counters_that_overflow_a_32_bit_total")
{
    const Aggregator aggregator;
    Delta delta;
    CdrRecord record = makeRecord(UsageType::D, 0);
    record.bytesReceived = 4294967296ULL;
    record.bytesTransmitted = 4294967296ULL;

    aggregator.fold({ record, record }, delta);

    CHECK(sub(delta, kMsisdn).data_rx == 8589934592ULL);
    CHECK(sub(delta, kMsisdn).data_tx == 8589934592ULL);
}
