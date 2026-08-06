#include "parser/csv_parser.hpp"

#include "constants.hpp"
#include "cdr_record.hpp"
#include "logger.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cdrp {

namespace {

/* Fills out with the fields of str. False when the line does not hold exactly kFieldCount */
bool split(std::string_view str, char delim, std::array<std::string_view, kFieldCount>& out)
{
    std::size_t start = 0;
    for (std::size_t i = 0; i < kFieldCount; ++i) {
        const std::size_t pos = str.find(delim, start);
        out[i] = str.substr(start, pos - start);
        if (pos == std::string_view::npos) {
            return i + 1 == kFieldCount; // ran out of fields early
        }
        start = pos + 1;
    }
    return false; // more fields than the format holds
}

bool parseU64(std::string_view str, uint64_t& out, bool optional = false, size_t maxDigits = 0)
{
    if (str.empty()) {
        out = 0;    // empty field = sentinel 0
        return optional;
    }
    if (maxDigits != 0 && str.size() > maxDigits) {
        return false;
    }
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), out);
    return ec == std::errc{} && ptr == str.data() + str.size();
}

std::optional<UsageType> parseUsage(std::string_view sv) {
    if (sv == "MOC")    return UsageType::MOC;
    if (sv == "MTC")    return UsageType::MTC;
    if (sv == "SMS-MO") return UsageType::SMS_MO;
    if (sv == "SMS-MT") return UsageType::SMS_MT;
    if (sv == "D")      return UsageType::D;
    if (sv == "U")      return UsageType::U;
    if (sv == "B")      return UsageType::B;
    if (sv == "X")      return UsageType::X;
    return std::nullopt;
}

// "DD/MM/YYYY" + "HH:MM:SS" -> time_t. Returns false on bad format.
bool parseDateTime(std::string_view date, std::string_view time, std::time_t& out) {
    if (date.size() != 10 || time.size() != 8) return false;
    if (date[2] != '/' || date[5] != '/') return false;
    if (time[2] != ':' || time[5] != ':') return false;
    std::tm tm{};
    // manual parse - avoid strptime locale dependence
    auto d2 = [](std::string_view s, size_t off) -> int {
        int v = 0;
        auto [p, ec] = std::from_chars(s.data() + off, s.data() + off + 2, v);
        return ec == std::errc{} ? v : -1;
    };
    tm.tm_mday = d2(date, 0);
    tm.tm_mon = d2(date, 3) - 1;
    int year = 0;
    auto [p, ec] = std::from_chars(date.data() + 6, date.data() + 10, year);
    if (ec != std::errc{}) return false;
    tm.tm_year = year - 1900;
    tm.tm_hour = d2(time, 0);
    tm.tm_min = d2(time, 3);
    tm.tm_sec = d2(time, 6);
    if (tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_mon < 0 || tm.tm_mon > 11) return false;
    if (tm.tm_hour < 0 || tm.tm_hour > 23) return false;
    if (tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0 || tm.tm_sec > 59) return false;
    out = timegm(&tm); // UTC, not local - avoid tz-dependent tests.
    return true;
}

std::optional<CdrRecord> invalidLine(std::string_view line)
{
    logDebug("CsvParser", "invalid line: " + std::string(line));
    return std::nullopt;
}

} // namespace

CsvParser::CsvParser(char separator)
    : m_sep(separator)
{
}

std::optional<CdrRecord> CsvParser::parse(std::string_view line) const
{
    std::array<std::string_view, kFieldCount> fields;
    std::optional<UsageType> usage;
    CdrRecord record;

    // stops on first fail
    const bool ok = split(line, m_sep, fields)
        && parseU64(fields[0], record.sequence)
        && parseU64(fields[1], record.subscriberImsi, false, kMaxImsiDigits)
        && (usage = parseUsage(fields[3])).has_value()
        && parseU64(fields[4], record.subscriberMSISDN, false, kMaxMsisdnDigits)
        && parseDateTime(fields[5], fields[6], record.callTime)
        && parseU64(fields[7], record.duration)
        && parseU64(fields[8], record.bytesReceived, true)
        && parseU64(fields[9], record.bytesTransmitted, true)
        && parseU64(fields[10], record.secondPartyIMSI, true, kMaxImsiDigits)
        && parseU64(fields[11], record.secondPartyMSISDN, true, kMaxMsisdnDigits);

    if (!ok) {
        return invalidLine(line);
    }

    record.usageType = *usage;
    record.subscriberImei = std::string(fields[2]);
    return record;
}

} // namespace cdrp

