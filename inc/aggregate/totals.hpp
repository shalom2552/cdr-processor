#pragma once

#include "cdr_record.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cdrp {

/**
 * What one batch passed through, counted per usage type rather than per subscriber.
 * Plain and unshared: folded on the hot path, merged into a RunTotals, then dropped.
 */
struct Totals {
    uint64_t records = 0;    // every record seen
    uint64_t moc_cnt = 0;    // outgoing calls
    uint64_t mtc_cnt = 0;    // incoming calls
    uint64_t sms_mo_cnt = 0; // outgoing messages
    uint64_t sms_mt_cnt = 0; // incoming messages
    uint64_t data_cnt = 0;   // data sessions
    uint64_t noans_cnt = 0;  // U
    uint64_t busy_cnt = 0;   // B
    uint64_t failed_cnt = 0; // X
    uint64_t moc_dur = 0;    // outgoing call seconds
    uint64_t mtc_dur = 0;    // incoming call seconds
    uint64_t data_dur = 0;   // data session seconds, which the aggregates never use
    uint64_t data_rx = 0;    // bytes received
    uint64_t data_tx = 0;    // bytes transmitted

    /**
     * Counts one record, whether or not it carries a subscriber MSISDN.
     *
     * @param record: the record to count
     */
    void add(const CdrRecord& record);

    /**
     * Counts every record of one batch.
     *
     * @param batch: the records to count
     */
    void add(const std::vector<CdrRecord>& batch);

    /**
     * Renders the counters for the message they are logged in, indented under it.
     *
     * @return fourteen lines, each opening with a tab, the name padded then the value
     */
    std::string format() const;
};

/**
 * The same counters for the whole run, added to from every thread.
 * One merge per batch, so no record touches an atomic.
 */
class RunTotals {
public:
    /**
     * Adds a folded batch in, one relaxed fetch_add per non-zero counter.
     *
     * @param totals: one batch's counters
     */
    void merge(const Totals& totals);

    /**
     * Reads the counters back. Fields are read one by one, so a snapshot taken while a
     * merge is in flight can hold part of that batch.
     *
     * @return the counters as they stand
     */
    Totals snapshot() const;

private:
    std::atomic<uint64_t> m_records { 0 };
    std::atomic<uint64_t> m_mocCnt { 0 };
    std::atomic<uint64_t> m_mtcCnt { 0 };
    std::atomic<uint64_t> m_smsMoCnt { 0 };
    std::atomic<uint64_t> m_smsMtCnt { 0 };
    std::atomic<uint64_t> m_dataCnt { 0 };
    std::atomic<uint64_t> m_noansCnt { 0 };
    std::atomic<uint64_t> m_busyCnt { 0 };
    std::atomic<uint64_t> m_failedCnt { 0 };
    std::atomic<uint64_t> m_mocDur { 0 };
    std::atomic<uint64_t> m_mtcDur { 0 };
    std::atomic<uint64_t> m_dataDur { 0 };
    std::atomic<uint64_t> m_dataRx { 0 };
    std::atomic<uint64_t> m_dataTx { 0 };
};

} // namespace cdrp

