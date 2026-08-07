#include "store/aggregate_writer.hpp"

#include "constants.hpp"
#include "logger.hpp"

#include <charconv>
#include <string>

namespace cdrp {

namespace {

/* Appends a decimal number, keeping the capacity the string already has */
void appendNumber(std::string& out, uint64_t value)
{
    char digits[20];
    char* const end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
    out.append(digits, static_cast<std::size_t>(end - digits));
}

} // namespace

AggregateWriter::AggregateWriter(IStore& store)
    : m_store(store)
{
}

bool AggregateWriter::add(std::string_view key, std::string_view field, uint64_t value)
{
    if (value == 0) return true; // nothing to add, do not spend a command on it
    return m_store.increment(key, field, value);
}

bool AggregateWriter::write(const Delta& delta)
{
    bool ok = true;
    std::string key;
    std::string field;

    for (const auto& [msisdn, sub] : delta.subs) {
        key.assign(kSubPrefix);
        appendNumber(key, msisdn);
        ok = add(key, kFieldVoiceOut, sub.voice_out) && ok;
        ok = add(key, kFieldVoiceIn, sub.voice_in) && ok;
        ok = add(key, kFieldDataRx, sub.data_rx) && ok;
        ok = add(key, kFieldDataTx, sub.data_tx) && ok;
        ok = add(key, kFieldSmsOut, sub.sms_out) && ok;
        ok = add(key, kFieldSmsIn, sub.sms_in) && ok;
        ok = add(key, kFieldNoans, sub.noans) && ok;
        ok = add(key, kFieldBusy, sub.busy) && ok;
        ok = add(key, kFieldFailed, sub.failed) && ok;
    }

    for (const auto& [mccmnc, op] : delta.ops) {
        key.assign(kOpPrefix);
        appendNumber(key, mccmnc);
        ok = add(key, kFieldVoiceOut, op.voice_out) && ok;
        ok = add(key, kFieldVoiceIn, op.voice_in) && ok;
        ok = add(key, kFieldSmsOut, op.sms_out) && ok;
        ok = add(key, kFieldSmsIn, op.sms_in) && ok;
    }

    for (const auto& [link, edge] : delta.links) {
        key.assign(kLinkPrefix);
        appendNumber(key, link.owner);
        field.clear();
        appendNumber(field, link.peer);
        const std::size_t peerEnd = field.size();
        field.append(kFieldDurSuffix);
        ok = add(key, field, edge.dur) && ok;
        field.resize(peerEnd);
        field.append(kFieldSmsSuffix);
        ok = add(key, field, edge.sms) && ok;
    }

    if (!m_store.flush() || !ok) {
        logWarn("AggregateWriter", "batch of " + std::to_string(delta.subs.size()) + " subscribers not fully written");
        return false;
    }

    logDebug("AggregateWriter", "wrote " + std::to_string(delta.subs.size()) + " subscribers, "
            + std::to_string(delta.ops.size()) + " operators, " + std::to_string(delta.links.size()) + " links");
    return true;
}

} // namespace cdrp

