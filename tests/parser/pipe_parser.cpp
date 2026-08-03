#include "doctest.h"
#include "constants.hpp"
#include "parser/iparser.hpp"
#include "parser/pipe_parser.hpp"

#include <ctime>
#include <memory>
#include <string>

namespace {

/* Builds the UTC timestamp a DD/MM/YYYY + HH:MM:SS pair denotes */
std::time_t makeTime(int day, int month, int year, int hour, int minute, int second)
{
    std::tm parts {};
    parts.tm_mday = day;
    parts.tm_mon = month - 1;
    parts.tm_year = year - 1900;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    return timegm(&parts);
}

/* A well formed outgoing voice call, used as the base for negative cases */
const std::string kVoiceLine =
    "2519|425020528409010|35-209900-176148-1|MOC|972528409042|"
    "12/07/2026|09:57:09|3314|||262040162782277|496221540";

/* A well formed data session: no second party, byte counters present */
const std::string kDataLine =
    "2520|425020528409010|35-209900-176148-1|D|972528409042|"
    "01/01/2026|00:00:00|120|8215|9273||";

/* Replaces the field at fieldIndex (0 based) of line with value */
std::string withField(const std::string& line, int fieldIndex, const std::string& value)
{
    std::string result;
    int current = 0;
    size_t start = 0;
    while (true) {
        const size_t end = line.find('|', start);
        const std::string field = line.substr(start, end - start);
        result += (current == fieldIndex) ? value : field;
        if (end == std::string::npos) {
            break;
        }
        result += '|';
        start = end + 1;
        ++current;
    }
    return result;
}

} // namespace

using namespace cdrp;

TEST_CASE("parser_reads_every_field_of_a_voice_record")
{
    const PipeParser parser;

    const auto record = parser.parse(kVoiceLine);

    REQUIRE(record.has_value());
    CHECK(record->sequence == 2519);
    CHECK(record->subscriberImsi == 425020528409010ULL);
    CHECK(record->subscriberImei == "35-209900-176148-1");
    CHECK(record->usageType == UsageType::MOC);
    CHECK(record->subscriberMSISDN == 972528409042ULL);
    CHECK(record->callTime == makeTime(12, 7, 2026, 9, 57, 9));
    CHECK(record->duration == 3314);
    CHECK(record->bytesReceived == 0);
    CHECK(record->bytesTransmitted == 0);
    CHECK(record->secondPartyIMSI == 262040162782277ULL);
    CHECK(record->secondPartyMSISDN == 496221540ULL);
}

TEST_CASE("parser_reads_every_field_of_a_data_record")
{
    const PipeParser parser;

    const auto record = parser.parse(kDataLine);

    REQUIRE(record.has_value());
    CHECK(record->usageType == UsageType::D);
    CHECK(record->duration == 120);
    CHECK(record->bytesReceived == 8215);
    CHECK(record->bytesTransmitted == 9273);
    CHECK(record->secondPartyIMSI == 0);
    CHECK(record->secondPartyMSISDN == 0);
}

TEST_CASE("parser_maps_every_usage_type")
{
    const PipeParser parser;

    const struct {
        const char* text;
        UsageType type;
    } cases[] = {
        { "MOC", UsageType::MOC },       { "MTC", UsageType::MTC },
        { "SMS-MO", UsageType::SMS_MO }, { "SMS-MT", UsageType::SMS_MT },
        { "D", UsageType::D },           { "U", UsageType::U },
        { "B", UsageType::B },           { "X", UsageType::X },
    };

    for (const auto& testCase : cases) {
        const auto record = parser.parse(withField(kVoiceLine, 3, testCase.text));
        REQUIRE(record.has_value());
        CHECK(record->usageType == testCase.type);
    }
}

TEST_CASE("parser_rejects_an_unknown_usage_type")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 3, "MOX")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 3, "moc")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 3, "")).has_value());
}

TEST_CASE("parser_rejects_a_wrong_field_count")
{
    const PipeParser parser;

    const std::string tooFew = "2519|425020528409010|35-209900-176148-1|MOC|972528409042|"
                               "12/07/2026|09:57:09|3314|||262040162782277";
    const std::string tooMany = kVoiceLine + "|extra";

    CHECK_FALSE(parser.parse(tooFew).has_value());
    CHECK_FALSE(parser.parse(tooMany).has_value());
}

TEST_CASE("parser_rejects_an_empty_line")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse("").has_value());
    CHECK_FALSE(parser.parse("|||||||||||").has_value());
}

TEST_CASE("parser_rejects_a_line_without_separators")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse("not a cdr record at all").has_value());
}

TEST_CASE("parser_rejects_non_numeric_numeric_fields")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 0, "abc")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 1, "42502052840901X")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 4, "+972528409042")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 7, "12.5")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 7, "-1")).has_value());
}

TEST_CASE("parser_rejects_missing_mandatory_fields")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 0, "")).has_value());  // sequence
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 1, "")).has_value());  // subscriber IMSI
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 4, "")).has_value());  // subscriber MSISDN
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "")).has_value());  // date
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "")).has_value());  // time
}

TEST_CASE("parser_rejects_an_imsi_longer_than_15_digits")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 1, "4250205284090100")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 10, "4250205284090100")).has_value());
}

TEST_CASE("parser_rejects_an_msisdn_longer_than_15_digits")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 4, "9725284090421234")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 11, "9725284090421234")).has_value());
}

TEST_CASE("parser_accepts_a_15_digit_imsi_and_msisdn")
{
    const PipeParser parser;

    std::string line = withField(kVoiceLine, 1, "425020528409010");
    line = withField(line, 4, "972528409042123");

    const auto record = parser.parse(line);

    REQUIRE(record.has_value());
    CHECK(record->subscriberImsi == 425020528409010ULL);
    CHECK(record->subscriberMSISDN == 972528409042123ULL);
}

TEST_CASE("parser_rejects_a_malformed_date")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "2026/07/12")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "12-07-2026")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "12/07/26")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "32/07/2026")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "12/13/2026")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 5, "00/07/2026")).has_value());
}

TEST_CASE("parser_rejects_a_malformed_time")
{
    const PipeParser parser;

    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "09:57")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "9:57:09")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "24:00:00")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "09:60:09")).has_value());
    CHECK_FALSE(parser.parse(withField(kVoiceLine, 6, "09:57:61")).has_value());
}

TEST_CASE("parser_keeps_date_and_time_together")
{
    const PipeParser parser;

    std::string line = withField(kVoiceLine, 5, "01/02/2020");
    line = withField(line, 6, "23:59:59");

    const auto record = parser.parse(line);

    REQUIRE(record.has_value());
    CHECK(record->callTime == makeTime(1, 2, 2020, 23, 59, 59));
}

TEST_CASE("parser_keeps_the_imei_verbatim")
{
    const PipeParser parser;

    const auto record = parser.parse(withField(kVoiceLine, 2, "35-209900-176148-1"));

    REQUIRE(record.has_value());
    CHECK(record->subscriberImei == "35-209900-176148-1");
}

TEST_CASE("parser_treats_empty_optional_fields_as_zero")
{
    const PipeParser parser;

    std::string line = withField(kVoiceLine, 8, "");
    line = withField(line, 9, "");
    line = withField(line, 10, "");
    line = withField(line, 11, "");

    const auto record = parser.parse(line);

    REQUIRE(record.has_value());
    CHECK(record->bytesReceived == 0);
    CHECK(record->bytesTransmitted == 0);
    CHECK(record->secondPartyIMSI == 0);
    CHECK(record->secondPartyMSISDN == 0);
}

TEST_CASE("parser_reads_a_zero_duration_for_unsuccessful_calls")
{
    const PipeParser parser;

    for (const char* type : { "U", "B", "X", "SMS-MO", "SMS-MT" }) {
        std::string line = withField(kVoiceLine, 3, type);
        line = withField(line, 7, "0");

        const auto record = parser.parse(line);

        REQUIRE(record.has_value());
        CHECK(record->duration == 0);
    }
}

TEST_CASE("parser_reads_large_counters")
{
    const PipeParser parser;

    std::string line = withField(kDataLine, 0, "18446744073709551615");
    line = withField(line, 8, "4294967296");
    line = withField(line, 9, "4294967296");

    const auto record = parser.parse(line);

    REQUIRE(record.has_value());
    CHECK(record->sequence == 18446744073709551615ULL);
    CHECK(record->bytesReceived == 4294967296ULL);
    CHECK(record->bytesTransmitted == 4294967296ULL);
}

TEST_CASE("parser_is_usable_through_the_iparser_interface")
{
    const std::unique_ptr<IParser> parser = std::make_unique<PipeParser>();

    const auto record = parser->parse(kVoiceLine);

    REQUIRE(record.has_value());
    CHECK(record->sequence == 2519);
}

TEST_CASE("parser_is_stateless_across_calls")
{
    const PipeParser parser;

    const auto first = parser.parse(kVoiceLine);
    CHECK_FALSE(parser.parse("garbage").has_value());
    const auto second = parser.parse(kVoiceLine);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->sequence == second->sequence);
    CHECK(first->callTime == second->callTime);
    CHECK(first->subscriberImei == second->subscriberImei);
}

TEST_CASE("parser_reads_a_record_out_of_a_larger_buffer")
{
    const PipeParser parser;

    const std::string buffer = kVoiceLine + "\n" + kDataLine + "\n";
    const std::string_view line(buffer.data(), kVoiceLine.size());

    const auto record = parser.parse(line);

    REQUIRE(record.has_value());
    CHECK(record->sequence == 2519);
}

TEST_CASE("record_field_count_matches_the_configured_constant")
{
    size_t fields = 1;
    for (const char character : kVoiceLine) {
        fields += (character == '|') ? 1 : 0;
    }

    CHECK(fields == kFieldCount);
}
