#include "doctest.h"
#include "ingest/iingestor.hpp"
#include "ingest/ingestor_factory.hpp"
#include "sink/isink.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

/* A sink the tests hand out, so a created ingestor can be tied back to it */
class NullSink : public cdrp::ISink {
public:
    void consume(std::vector<cdrp::CdrRecord>&) override
    {
    }
};

/* An ingestor the tests register, recognisable by its marker and the sink it was given */
class StubIngestor : public cdrp::IIngestor {
public:
    StubIngestor(uint64_t marker, cdrp::ISink& sink)
        : m_marker(marker)
        , m_sink(sink)
    {
    }

    bool start() override
    {
        return true;
    }

    void stop() override
    {
    }

    uint64_t marker() const
    {
        return m_marker;
    }

    const cdrp::ISink* sink() const
    {
        return &m_sink;
    }

private:
    uint64_t m_marker;
    cdrp::ISink& m_sink;
};

} // namespace

using namespace cdrp;

TEST_CASE("ingestor_factory_hands_out_one_shared_instance")
{
    CHECK(&IngestorFactory::instance() == &IngestorFactory::instance());
}

TEST_CASE("ingestor_factory_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<IngestorFactory>::value);
    CHECK_FALSE(std::is_copy_assignable<IngestorFactory>::value);
}

TEST_CASE("ingestor_factory_has_the_configured_modes_registered_by_default")
{
    IngestorFactory& factory = IngestorFactory::instance();

    CHECK(factory.hasIngestor("file"));
    CHECK(factory.hasIngestor("rabbit"));
}

TEST_CASE("ingestor_factory_builds_the_ingestor_of_every_registered_mode")
{
    IngestorFactory& factory = IngestorFactory::instance();
    NullSink sink;

    CHECK(factory.createIngestor("file", sink) != nullptr);
    CHECK(factory.createIngestor("rabbit", sink) != nullptr);
}

TEST_CASE("ingestor_factory_reports_an_unregistered_mode_as_absent")
{
    CHECK_FALSE(IngestorFactory::instance().hasIngestor("no_such_mode_for_absent_test"));
}

TEST_CASE("ingestor_factory_returns_null_for_an_unknown_mode")
{
    NullSink sink;

    CHECK(IngestorFactory::instance().createIngestor("no_such_mode_for_null_test", sink) == nullptr);
}

TEST_CASE("ingestor_factory_registers_and_creates_a_custom_ingestor")
{
    IngestorFactory& factory = IngestorFactory::instance();
    const std::string name = "stub_custom_ingestor";
    NullSink sink;

    factory.registerIngestor(name, [](ISink& s) { return std::make_unique<StubIngestor>(4242, s); });

    CHECK(factory.hasIngestor(name));

    const auto ingestor = factory.createIngestor(name, sink);
    REQUIRE(ingestor != nullptr);

    CHECK(static_cast<StubIngestor*>(ingestor.get())->marker() == 4242);
}

TEST_CASE("ingestor_factory_hands_the_ingestor_the_sink_it_was_given")
{
    IngestorFactory& factory = IngestorFactory::instance();
    const std::string name = "stub_sink_ingestor";
    NullSink first;
    NullSink second;

    factory.registerIngestor(name, [](ISink& s) { return std::make_unique<StubIngestor>(1, s); });

    const auto one = factory.createIngestor(name, first);
    const auto two = factory.createIngestor(name, second);
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);

    CHECK(static_cast<StubIngestor*>(one.get())->sink() == &first);
    CHECK(static_cast<StubIngestor*>(two.get())->sink() == &second);
}

TEST_CASE("ingestor_factory_creates_a_fresh_instance_on_every_call")
{
    IngestorFactory& factory = IngestorFactory::instance();
    NullSink sink;
    factory.registerIngestor("stub_fresh_ingestor", [](ISink& s) { return std::make_unique<StubIngestor>(1, s); });

    const auto first = factory.createIngestor("stub_fresh_ingestor", sink);
    const auto second = factory.createIngestor("stub_fresh_ingestor", sink);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first.get() != second.get());
}

TEST_CASE("ingestor_factory_replaces_an_ingestor_registered_under_the_same_name")
{
    IngestorFactory& factory = IngestorFactory::instance();
    const std::string name = "stub_replaced_ingestor";
    NullSink sink;

    factory.registerIngestor(name, [](ISink& s) { return std::make_unique<StubIngestor>(1, s); });
    factory.registerIngestor(name, [](ISink& s) { return std::make_unique<StubIngestor>(2, s); });

    const auto ingestor = factory.createIngestor(name, sink);
    REQUIRE(ingestor != nullptr);

    CHECK(static_cast<StubIngestor*>(ingestor.get())->marker() == 2);
}

TEST_CASE("ingestor_factory_hands_back_an_ingestor_usable_through_the_interface")
{
    IngestorFactory& factory = IngestorFactory::instance();
    NullSink sink;
    factory.registerIngestor("stub_interface_ingestor", [](ISink& s) { return std::make_unique<StubIngestor>(1, s); });

    const std::unique_ptr<IIngestor> ingestor = factory.createIngestor("stub_interface_ingestor", sink);
    REQUIRE(ingestor != nullptr);

    CHECK(ingestor->start());
    ingestor->stop();
}
