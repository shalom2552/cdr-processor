#include "aggregate/rank_writer.hpp"

#include "constants.hpp"
#include "logger.hpp"

#include <charconv>
#include <cstddef>
#include <string>

constexpr std::string_view kComponent = "RankWriter";

namespace cdrp {

RankWriter::RankWriter(IStore& store)
    : m_store(store)
{
}

void RankWriter::appendNumber(std::string& out, uint64_t value)
{
    char digits[20];
    char* const end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
    out.append(digits, static_cast<std::size_t>(end - digits));
}

bool RankWriter::add(std::string_view board, std::string_view member, uint64_t value)
{
    if (value == 0) return true; // nothing to add, do not spend a command on it
    return m_store.rank(board, member, value);
}

bool RankWriter::write(const Delta& delta)
{
    bool ok = true;
    std::string member;

    for (const auto& [msisdn, sub] : delta.subs) {
        member.clear();
        appendNumber(member, msisdn);
        ok = add(kVoiceBoard, member, sub.voice_out + sub.voice_in) && ok;
        ok = add(kSmsBoard, member, sub.sms_out + sub.sms_in) && ok;
        ok = add(kDataBoard, member, sub.data_rx + sub.data_tx) && ok;
        ok = add(kFailBoard, member, sub.noans + sub.busy + sub.failed) && ok;
    }

    for (const auto& [mccmnc, op] : delta.ops) {
        member.clear();
        appendNumber(member, mccmnc);
        ok = add(kOpVoiceBoard, member, op.voice_out + op.voice_in) && ok;
        ok = add(kOpSmsBoard, member, op.sms_out + op.sms_in) && ok;
    }

    if (!ok) {
        logWarn(kComponent, "boards of " + std::to_string(delta.subs.size()) + " subscribers not fully written");
        return false;
    }

    logDebug(kComponent, "ranked " + std::to_string(delta.subs.size()) + " subscribers, "
            + std::to_string(delta.ops.size()) + " operators");
    return true;
}

} // namespace cdrp
