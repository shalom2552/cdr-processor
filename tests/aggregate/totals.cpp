#include "doctest.h"
#include "aggregate/totals.hpp"
#include "constants.hpp"

#include <cstdint>
#include <thread>
#include <vector>

namespace {

using namespace cdrp;

constexpr uint64_t kImsi = 425020528409010ULL;
constexpr uint64_t kMsisdn = 972528409042ULL;

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
    record.secondPartyMSISDN = 496221540ULL;
    return record;
}

/* One data session of the given duration and byte counts */
CdrRecord makeDataRecord(uint64_t duration, uint64_t rx, uint64_t tx)
{
    CdrRecord record = makeRecord(UsageType::D, duration);
    record.bytesReceived = rx;
    record.bytesTransmitted = tx;
    return record;
}

} // namespace

using namespace cdrp;

TEST_CASE("totals_start_at_zero")
{
    const Totals totals;

    CHECK(totals.records == 0);
    CHECK(totals.moc_cnt == 0);
    CHECK(totals.data_rx == 0);
    CHECK(totals.data_tx == 0);
}

TEST_CASE("totals_count_an_outgoing_call_and_its_seconds")
{
    Totals totals;

    totals.add(makeRecord(UsageType::MOC, 3314));

    CHECK(totals.records == 1);
    CHECK(totals.moc_cnt == 1);
    CHECK(totals.moc_dur == 3314);
    CHECK(totals.mtc_cnt == 0);
    CHECK(totals.mtc_dur == 0);
}

TEST_CASE("totals_count_an_incoming_call_and_its_seconds")
{
    Totals totals;

    totals.add(makeRecord(UsageType::MTC, 120));

    CHECK(totals.mtc_cnt == 1);
    CHECK(totals.mtc_dur == 120);
    CHECK(totals.moc_cnt == 0);
}

TEST_CASE("totals_count_a_message_each_way_without_seconds")
{
    Totals totals;

    totals.add(makeRecord(UsageType::SMS_MO, 60));
    totals.add(makeRecord(UsageType::SMS_MT, 60));

    CHECK(totals.sms_mo_cnt == 1);
    CHECK(totals.sms_mt_cnt == 1);
    CHECK(totals.moc_dur == 0);
    CHECK(totals.mtc_dur == 0);
    CHECK(totals.data_dur == 0);
}

TEST_CASE("totals_count_a_data_session_with_its_seconds_and_bytes")
{
    Totals totals;

    totals.add(makeDataRecord(120, 8215, 9273));

    CHECK(totals.data_cnt == 1);
    CHECK(totals.data_dur == 120);
    CHECK(totals.data_rx == 8215);
    CHECK(totals.data_tx == 9273);
}

TEST_CASE("totals_keep_unanswered_busy_and_failed_calls_apart")
{
    Totals totals;

    totals.add(makeRecord(UsageType::U));
    totals.add(makeRecord(UsageType::B));
    totals.add(makeRecord(UsageType::X));

    CHECK(totals.records == 3);
    CHECK(totals.noans_cnt == 1);
    CHECK(totals.busy_cnt == 1);
    CHECK(totals.failed_cnt == 1);
    CHECK(totals.moc_cnt == 0);
}

TEST_CASE("totals_count_a_record_without_a_subscriber_msisdn")
{
    Totals totals;
    CdrRecord record = makeRecord(UsageType::MOC, 30);
    record.subscriberMSISDN = 0;

    totals.add(record);

    CHECK(totals.records == 1);
    CHECK(totals.moc_cnt == 1);
    CHECK(totals.moc_dur == 30);
}

TEST_CASE("totals_count_every_record_of_a_batch")
{
    Totals totals;
    const std::vector<CdrRecord> batch { makeRecord(UsageType::MOC, 10),
        makeRecord(UsageType::MOC, 30), makeRecord(UsageType::MTC, 5),
        makeRecord(UsageType::SMS_MO), makeDataRecord(60, 100, 200) };

    totals.add(batch);

    CHECK(totals.records == 5);
    CHECK(totals.moc_cnt == 2);
    CHECK(totals.moc_dur == 40);
    CHECK(totals.mtc_cnt == 1);
    CHECK(totals.mtc_dur == 5);
    CHECK(totals.sms_mo_cnt == 1);
    CHECK(totals.data_cnt == 1);
    CHECK(totals.data_dur == 60);
}

TEST_CASE("totals_leave_an_empty_batch_alone")
{
    Totals totals;

    totals.add(std::vector<CdrRecord> {});

    CHECK(totals.records == 0);
}

TEST_CASE("totals_add_a_second_batch_onto_the_first")
{
    Totals totals;

    totals.add(std::vector<CdrRecord> { makeRecord(UsageType::MOC, 10) });
    totals.add(std::vector<CdrRecord> { makeRecord(UsageType::MOC, 30) });

    CHECK(totals.records == 2);
    CHECK(totals.moc_cnt == 2);
    CHECK(totals.moc_dur == 40);
}

TEST_CASE("totals_take_a_full_batch")
{
    Totals totals;
    std::vector<CdrRecord> batch;
    for (std::size_t index = 0; index < kBatchSize; ++index) {
        batch.push_back(makeRecord(UsageType::MOC, 1));
    }

    totals.add(batch);

    CHECK(totals.records == kBatchSize);
    CHECK(totals.moc_cnt == kBatchSize);
    CHECK(totals.moc_dur == kBatchSize);
}

TEST_CASE("totals_add_counters_that_overflow_a_32_bit_total")
{
    Totals totals;
    const CdrRecord record = makeDataRecord(0, 4294967296ULL, 4294967296ULL);

    totals.add(std::vector<CdrRecord> { record, record });

    CHECK(totals.data_rx == 8589934592ULL);
    CHECK(totals.data_tx == 8589934592ULL);
}

TEST_CASE("totals_format_every_field_of_an_empty_run")
{
    const Totals totals;

    CHECK(totals.format() ==
        "\n\trecords     0"
        "\n\tmoc_cnt     0"
        "\n\tmtc_cnt     0"
        "\n\tsms_mo_cnt  0"
        "\n\tsms_mt_cnt  0"
        "\n\tdata_cnt    0"
        "\n\tnoans_cnt   0"
        "\n\tbusy_cnt    0"
        "\n\tfailed_cnt  0"
        "\n\tmoc_dur     0"
        "\n\tmtc_dur     0"
        "\n\tdata_dur    0"
        "\n\tdata_rx     0"
        "\n\tdata_tx     0");
}

TEST_CASE("totals_format_the_values_they_hold")
{
    Totals totals;
    totals.add(makeRecord(UsageType::MOC, 3314));
    totals.add(makeDataRecord(120, 8215, 9273));

    CHECK(totals.format() ==
        "\n\trecords     2"
        "\n\tmoc_cnt     1"
        "\n\tmtc_cnt     0"
        "\n\tsms_mo_cnt  0"
        "\n\tsms_mt_cnt  0"
        "\n\tdata_cnt    1"
        "\n\tnoans_cnt   0"
        "\n\tbusy_cnt    0"
        "\n\tfailed_cnt  0"
        "\n\tmoc_dur     3314"
        "\n\tmtc_dur     0"
        "\n\tdata_dur    120"
        "\n\tdata_rx     8215"
        "\n\tdata_tx     9273");
}

TEST_CASE("run_totals_start_at_zero")
{
    const RunTotals run;

    CHECK(run.snapshot().records == 0);
    CHECK(run.snapshot().moc_cnt == 0);
}

TEST_CASE("run_totals_take_every_field_of_a_merge")
{
    RunTotals run;
    Totals batch;
    batch.add(makeRecord(UsageType::MOC, 3314));
    batch.add(makeRecord(UsageType::MTC, 120));
    batch.add(makeRecord(UsageType::SMS_MO));
    batch.add(makeRecord(UsageType::SMS_MT));
    batch.add(makeDataRecord(60, 8215, 9273));
    batch.add(makeRecord(UsageType::U));
    batch.add(makeRecord(UsageType::B));
    batch.add(makeRecord(UsageType::X));

    run.merge(batch);

    const Totals taken = run.snapshot();
    CHECK(taken.records == 8);
    CHECK(taken.moc_cnt == 1);
    CHECK(taken.mtc_cnt == 1);
    CHECK(taken.sms_mo_cnt == 1);
    CHECK(taken.sms_mt_cnt == 1);
    CHECK(taken.data_cnt == 1);
    CHECK(taken.noans_cnt == 1);
    CHECK(taken.busy_cnt == 1);
    CHECK(taken.failed_cnt == 1);
    CHECK(taken.moc_dur == 3314);
    CHECK(taken.mtc_dur == 120);
    CHECK(taken.data_dur == 60);
    CHECK(taken.data_rx == 8215);
    CHECK(taken.data_tx == 9273);
}

TEST_CASE("run_totals_add_one_merge_onto_the_next")
{
    RunTotals run;
    Totals batch;
    batch.add(makeRecord(UsageType::MOC, 10));

    run.merge(batch);
    run.merge(batch);

    CHECK(run.snapshot().records == 2);
    CHECK(run.snapshot().moc_dur == 20);
}

TEST_CASE("run_totals_are_unmoved_by_an_empty_merge")
{
    RunTotals run;
    Totals batch;
    batch.add(makeRecord(UsageType::MOC, 10));

    run.merge(batch);
    run.merge(Totals {});

    CHECK(run.snapshot().records == 1);
    CHECK(run.snapshot().moc_cnt == 1);
}

TEST_CASE("run_totals_keep_every_merge_of_every_thread")
{
    constexpr int kThreads = 8;
    constexpr int kMerges = 1000;
    RunTotals run;
    Totals batch;
    batch.add(makeRecord(UsageType::MOC, 3));
    batch.add(makeDataRecord(0, 5, 7));

    std::vector<std::thread> threads;
    for (int index = 0; index < kThreads; ++index) {
        threads.emplace_back([&run, &batch] {
            for (int merge = 0; merge < kMerges; ++merge) {
                run.merge(batch);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const Totals taken = run.snapshot();
    CHECK(taken.records == 2 * kThreads * kMerges);
    CHECK(taken.moc_cnt == kThreads * kMerges);
    CHECK(taken.moc_dur == 3 * kThreads * kMerges);
    CHECK(taken.data_rx == 5 * kThreads * kMerges);
    CHECK(taken.data_tx == 7 * kThreads * kMerges);
}

TEST_CASE("run_totals_snapshot_leaves_the_counters_where_they_were")
{
    RunTotals run;
    Totals batch;
    batch.add(makeRecord(UsageType::MOC, 10));

    run.merge(batch);
    run.snapshot();

    CHECK(run.snapshot().records == 1);
    CHECK(run.snapshot().moc_dur == 10);
}
