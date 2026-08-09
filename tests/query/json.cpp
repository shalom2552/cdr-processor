#include "doctest.h"
#include "query/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace cdrp;

TEST_CASE("json_starts_as_an_empty_object")
{
    const Json json;

    CHECK(json.str() == "{}");
}

TEST_CASE("json_writes_a_string_field")
{
    Json json;

    json.add("msisdn", "972528409042");

    CHECK(json.str() == R"({"msisdn":"972528409042"})");
}

TEST_CASE("json_writes_a_number_field")
{
    Json json;

    json.add("calls", uint64_t(42));

    CHECK(json.str() == R"({"calls":42})");
}

TEST_CASE("json_writes_an_array_field")
{
    Json json;

    json.add("peers", std::vector<std::string> { "972500000001", "972500000002" });

    CHECK(json.str() == R"({"peers":["972500000001","972500000002"]})");
}

TEST_CASE("json_writes_an_empty_array")
{
    Json json;

    json.add("peers", std::vector<std::string> {});

    CHECK(json.str() == R"({"peers":[]})");
}

TEST_CASE("json_writes_an_array_of_one")
{
    Json json;

    json.add("peers", std::vector<std::string> { "972500000001" });

    CHECK(json.str() == R"({"peers":["972500000001"]})");
}

TEST_CASE("json_separates_fields_with_a_comma")
{
    Json json;

    json.add("msisdn", "972528409042");
    json.add("calls", uint64_t(3));
    json.add("peers", std::vector<std::string> { "972500000001" });

    CHECK(json.str() == R"({"msisdn":"972528409042","calls":3,"peers":["972500000001"]})");
}

TEST_CASE("json_keeps_the_order_fields_were_added")
{
    Json first;
    Json second;

    first.add("a", uint64_t(1)).add("b", uint64_t(2));
    second.add("b", uint64_t(2)).add("a", uint64_t(1));

    CHECK(first.str() == R"({"a":1,"b":2})");
    CHECK(second.str() == R"({"b":2,"a":1})");
}

TEST_CASE("json_chains_adds_on_the_same_object")
{
    Json json;

    Json& chained = json.add("a", "x").add("b", uint64_t(1));

    CHECK(&chained == &json);
    CHECK(json.str() == R"({"a":"x","b":1})");
}

TEST_CASE("json_writes_the_whole_number_range")
{
    Json json;

    json.add("zero", uint64_t(0));
    json.add("max", uint64_t(18446744073709551615ULL));

    CHECK(json.str() == R"({"zero":0,"max":18446744073709551615})");
}

TEST_CASE("json_accepts_an_empty_name_and_an_empty_value")
{
    Json json;

    json.add("", "");

    CHECK(json.str() == R"({"":""})");
}

TEST_CASE("json_escapes_quotes_and_backslashes_in_values")
{
    Json json;

    json.add("name", R"(a"b\c)");

    CHECK(json.str() == R"({"name":"a\"b\\c"})");
}

TEST_CASE("json_escapes_quotes_and_backslashes_in_names")
{
    Json json;

    json.add(R"(a"b)", "x");

    CHECK(json.str() == R"({"a\"b":"x"})");
}

TEST_CASE("json_escapes_quotes_and_backslashes_inside_arrays")
{
    Json json;

    json.add("peers", std::vector<std::string> { R"(a"b)", R"(c\d)" });

    CHECK(json.str() == R"({"peers":["a\"b","c\\d"]})");
}

TEST_CASE("json_escapes_control_characters")
{
    Json json;

    json.add("raw", std::string_view("a\nb\tc\x01", 6));

    CHECK(json.str() == "{\"raw\":\"a\\u000ab\\u0009c\\u0001\"}");
}

TEST_CASE("json_keeps_non_ascii_bytes_verbatim")
{
    Json json;

    json.add("name", "\xc3\xa9");

    CHECK(json.str() == "{\"name\":\"\xc3\xa9\"}");
}

TEST_CASE("json_reads_a_value_out_of_a_larger_buffer")
{
    const std::string buffer = "972528409042 and more text";
    const std::string_view value(buffer.data(), 12);
    Json json;

    json.add("msisdn", value);

    CHECK(json.str() == R"({"msisdn":"972528409042"})");
}

TEST_CASE("json_str_leaves_the_object_usable")
{
    Json json;

    json.add("a", uint64_t(1));
    const std::string first = json.str();
    const std::string second = json.str();
    json.add("b", uint64_t(2));

    CHECK(first == second);
    CHECK(json.str() == R"({"a":1,"b":2})");
}

TEST_CASE("json_holds_many_fields")
{
    Json json;
    std::string expected = "{";

    for (int index = 0; index < 1000; ++index) {
        const std::string name = "f" + std::to_string(index);
        json.add(name, uint64_t(index));
        expected += (index == 0 ? "" : ",") + ('"' + name + "\":" + std::to_string(index));
    }
    expected += "}";

    CHECK(json.str() == expected);
}

TEST_CASE("json_error_wraps_the_message_in_an_object")
{
    CHECK(Json::error("subscriber not found") == R"({"error":"subscriber not found"})");
}

TEST_CASE("json_error_escapes_the_message")
{
    CHECK(Json::error(R"(bad "input")") == R"({"error":"bad \"input\""})");
}

TEST_CASE("json_error_accepts_an_empty_message")
{
    CHECK(Json::error("") == R"({"error":""})");
}
