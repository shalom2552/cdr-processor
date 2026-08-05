#include "doctest.h"
#include "parser/parser_factory.hpp"
#include "parser/iparser.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

/* A parser the tests register so a created instance is recognisable by its output */
class StubParser : public cdrp::IParser {
public:
    explicit StubParser(uint64_t marker)
        : m_marker(marker)
    {
    }

    std::optional<cdrp::CdrRecord> parse(std::string_view) const override
    {
        cdrp::CdrRecord record {};
        record.sequence = m_marker;
        return record;
    }

private:
    uint64_t m_marker;
};

} // namespace

using namespace cdrp;

TEST_CASE("parser_factory_hands_out_one_shared_instance")
{
    CHECK(&ParserFactory::instance() == &ParserFactory::instance());
}

TEST_CASE("parser_factory_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<ParserFactory>::value);
    CHECK_FALSE(std::is_copy_assignable<ParserFactory>::value);
}

TEST_CASE("parser_factory_has_the_pipe_parser_registered_by_default")
{
    ParserFactory& factory = ParserFactory::instance();

    CHECK(factory.hasParser("pipe"));
    CHECK(factory.createParser("pipe") != nullptr);
}

TEST_CASE("parser_factory_reports_an_unregistered_name_as_absent")
{
    CHECK_FALSE(ParserFactory::instance().hasParser("no_such_parser_for_absent_test"));
}

TEST_CASE("parser_factory_returns_null_for_an_unknown_parser")
{
    CHECK(ParserFactory::instance().createParser("no_such_parser_for_null_test") == nullptr);
}

TEST_CASE("parser_factory_registers_and_creates_a_custom_parser")
{
    ParserFactory& factory = ParserFactory::instance();
    const std::string name = "stub_custom_parser";

    factory.registerParser(name, [] { return std::make_unique<StubParser>(4242); });

    CHECK(factory.hasParser(name));

    const auto parser = factory.createParser(name);
    REQUIRE(parser != nullptr);

    const auto record = parser->parse("anything");
    REQUIRE(record.has_value());
    CHECK(record->sequence == 4242);
}

TEST_CASE("parser_factory_creates_a_fresh_instance_on_every_call")
{
    ParserFactory& factory = ParserFactory::instance();
    factory.registerParser("stub_fresh_parser", [] { return std::make_unique<StubParser>(1); });

    const auto first = factory.createParser("stub_fresh_parser");
    const auto second = factory.createParser("stub_fresh_parser");

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first.get() != second.get());
}

TEST_CASE("parser_factory_replaces_a_parser_registered_under_the_same_name")
{
    ParserFactory& factory = ParserFactory::instance();
    const std::string name = "stub_replaced_parser";

    factory.registerParser(name, [] { return std::make_unique<StubParser>(1); });
    factory.registerParser(name, [] { return std::make_unique<StubParser>(2); });

    const auto parser = factory.createParser(name);
    REQUIRE(parser != nullptr);

    const auto record = parser->parse("anything");
    REQUIRE(record.has_value());
    CHECK(record->sequence == 2);
}
