#include "doctest.h"
#include "query/query_factory.hpp"
#include "query/iquery_store.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

/* A query store the tests register so a created instance is recognisable by what it answers */
class StubQuery : public cdrp::IQueryStore {
public:
    explicit StubQuery(std::string marker)
        : m_marker(std::move(marker))
    {
    }

    bool hgetall(const std::string_view, Fields& out) const override
    {
        out.clear();
        out.emplace_back("marker", m_marker);
        return true;
    }

    bool hkeys(const std::string_view, std::vector<std::string>& out) const override
    {
        out.clear();
        out.push_back(m_marker);
        return true;
    }

    bool hmget(const std::string_view, const std::vector<std::string>&,
               std::vector<std::string>& out) const override
    {
        out.clear();
        out.push_back(m_marker);
        return true;
    }

private:
    std::string m_marker;
};

/* The marker the stub registered under name answers with */
std::string markerOf(const cdrp::IQueryStore& store)
{
    std::vector<std::string> out;
    store.hkeys("any", out);
    return out.empty() ? std::string() : out.front();
}

} // namespace

using namespace cdrp;

TEST_CASE("query_factory_hands_out_one_shared_instance")
{
    CHECK(&QueryFactory::instance() == &QueryFactory::instance());
}

TEST_CASE("query_factory_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<QueryFactory>::value);
    CHECK_FALSE(std::is_copy_assignable<QueryFactory>::value);
}

TEST_CASE("query_factory_has_the_redis_query_registered_by_default")
{
    QueryFactory& factory = QueryFactory::instance();

    CHECK(factory.hasQuery("redis"));
    CHECK(factory.createQuery("redis") != nullptr);
}

TEST_CASE("query_factory_reports_an_unregistered_name_as_absent")
{
    CHECK_FALSE(QueryFactory::instance().hasQuery("no_such_query_for_absent_test"));
}

TEST_CASE("query_factory_returns_null_for_an_unknown_query_store")
{
    CHECK(QueryFactory::instance().createQuery("no_such_query_for_null_test") == nullptr);
}

TEST_CASE("query_factory_registers_and_creates_a_custom_query_store")
{
    QueryFactory& factory = QueryFactory::instance();
    const std::string name = "stub_custom_query";

    factory.registerQuery(name, [] { return std::make_unique<StubQuery>("custom"); });

    CHECK(factory.hasQuery(name));

    const auto store = factory.createQuery(name);
    REQUIRE(store != nullptr);

    CHECK(markerOf(*store) == "custom");
}

TEST_CASE("query_factory_creates_a_fresh_instance_on_every_call")
{
    QueryFactory& factory = QueryFactory::instance();
    factory.registerQuery("stub_fresh_query", [] { return std::make_unique<StubQuery>("fresh"); });

    const auto first = factory.createQuery("stub_fresh_query");
    const auto second = factory.createQuery("stub_fresh_query");

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first.get() != second.get());
}

TEST_CASE("query_factory_replaces_a_query_store_registered_under_the_same_name")
{
    QueryFactory& factory = QueryFactory::instance();
    const std::string name = "stub_replaced_query";

    factory.registerQuery(name, [] { return std::make_unique<StubQuery>("first"); });
    factory.registerQuery(name, [] { return std::make_unique<StubQuery>("second"); });

    const auto store = factory.createQuery(name);
    REQUIRE(store != nullptr);

    CHECK(markerOf(*store) == "second");
}
