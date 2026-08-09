#include "aggregate/totals.hpp"

#include "constants.hpp"

#include <charconv>
#include <string_view>

namespace cdrp {

namespace {

/* Appends one indented line, the name padded out and the value in plain decimal */
void appendField(std::string& out, std::string_view name, uint64_t value)
{
    out.append("\n\t");
    out.append(name);
    out.append(kTotalsNameWidth - name.size(), ' ');

    char digits[20];
    char* const end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
    out.append(digits, static_cast<std::size_t>(end - digits));
}

/* Adds value to counter, spending nothing on the counters a batch never touched */
void bump(std::atomic<uint64_t>& counter, uint64_t value)
{
    if (value == 0) return;
    counter.fetch_add(value, std::memory_order_relaxed);
}

} // namespace

void Totals::add(const CdrRecord& record)
{
    ++records;

    switch (record.usageType) {
        case UsageType::MOC:
            ++moc_cnt;
            moc_dur += record.duration;
            break;

        case UsageType::MTC:
            ++mtc_cnt;
            mtc_dur += record.duration;
            break;

        case UsageType::SMS_MO:
            ++sms_mo_cnt;
            break;

        case UsageType::SMS_MT:
            ++sms_mt_cnt;
            break;

        case UsageType::D:
            ++data_cnt;
            data_dur += record.duration;
            data_rx += record.bytesReceived;
            data_tx += record.bytesTransmitted;
            break;

        case UsageType::U:
            ++noans_cnt;
            break;

        case UsageType::B:
            ++busy_cnt;
            break;

        case UsageType::X:
            ++failed_cnt;
            break;
    }
}

void Totals::add(const std::vector<CdrRecord>& batch)
{
    for (const CdrRecord& record : batch) {
        add(record);
    }
}

std::string Totals::format() const
{
    std::string out;
    out.reserve(14 * (kTotalsNameWidth + 14));

    appendField(out, kFieldRecords, records);
    appendField(out, kFieldMocCnt, moc_cnt);
    appendField(out, kFieldMtcCnt, mtc_cnt);
    appendField(out, kFieldSmsMoCnt, sms_mo_cnt);
    appendField(out, kFieldSmsMtCnt, sms_mt_cnt);
    appendField(out, kFieldDataCnt, data_cnt);
    appendField(out, kFieldNoansCnt, noans_cnt);
    appendField(out, kFieldBusyCnt, busy_cnt);
    appendField(out, kFieldFailedCnt, failed_cnt);
    appendField(out, kFieldMocDur, moc_dur);
    appendField(out, kFieldMtcDur, mtc_dur);
    appendField(out, kFieldDataDur, data_dur);
    appendField(out, kFieldDataRx, data_rx);
    appendField(out, kFieldDataTx, data_tx);

    return out;
}

void RunTotals::merge(const Totals& totals)
{
    bump(m_records, totals.records);
    bump(m_mocCnt, totals.moc_cnt);
    bump(m_mtcCnt, totals.mtc_cnt);
    bump(m_smsMoCnt, totals.sms_mo_cnt);
    bump(m_smsMtCnt, totals.sms_mt_cnt);
    bump(m_dataCnt, totals.data_cnt);
    bump(m_noansCnt, totals.noans_cnt);
    bump(m_busyCnt, totals.busy_cnt);
    bump(m_failedCnt, totals.failed_cnt);
    bump(m_mocDur, totals.moc_dur);
    bump(m_mtcDur, totals.mtc_dur);
    bump(m_dataDur, totals.data_dur);
    bump(m_dataRx, totals.data_rx);
    bump(m_dataTx, totals.data_tx);
}

Totals RunTotals::snapshot() const
{
    Totals out;

    out.records = m_records.load(std::memory_order_relaxed);
    out.moc_cnt = m_mocCnt.load(std::memory_order_relaxed);
    out.mtc_cnt = m_mtcCnt.load(std::memory_order_relaxed);
    out.sms_mo_cnt = m_smsMoCnt.load(std::memory_order_relaxed);
    out.sms_mt_cnt = m_smsMtCnt.load(std::memory_order_relaxed);
    out.data_cnt = m_dataCnt.load(std::memory_order_relaxed);
    out.noans_cnt = m_noansCnt.load(std::memory_order_relaxed);
    out.busy_cnt = m_busyCnt.load(std::memory_order_relaxed);
    out.failed_cnt = m_failedCnt.load(std::memory_order_relaxed);
    out.moc_dur = m_mocDur.load(std::memory_order_relaxed);
    out.mtc_dur = m_mtcDur.load(std::memory_order_relaxed);
    out.data_dur = m_dataDur.load(std::memory_order_relaxed);
    out.data_rx = m_dataRx.load(std::memory_order_relaxed);
    out.data_tx = m_dataTx.load(std::memory_order_relaxed);

    return out;
}

} // namespace cdrp

