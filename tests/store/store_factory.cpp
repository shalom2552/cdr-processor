#include "doctest.h"
#include "store/store_factory.hpp"
#include "store/istore.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

/* A store the tests register so a created instance is recognisable by what it counted */
class StubStore : public cdrp::IStore {
public:
    explicit StubStore(uint64_t marker)
        : m_marker(marker)
    {
    }

    bool increment(std::string_view, std::string_view, uint64_t value) override
    {
        m_counted += value * m_marker;
        return true;
    }

    bool rank(std::string_view, std::string_view, uint64_t) override
    {
        return true;
    }

    bool flush() override
    {
        return true;
    }

    uint64_t resume_at(std::string_view) override
    {
        return 0;
    }

    bool mark(std::string_view, uint64_t) override
    {
        return true;
    }

    /* What increment() added up to, the marker times every value it took */
    uint64_t counted() const
    {
        return m_counted;
    }

private:
    uint64_t m_marker;
    uint64_t m_counted = 0;
};

} // namespace

using namespace cdrp;

TEST_CASE("store_factory_hands_out_one_shared_instance")
{
    CHECK(&StoreFactory::instance() == &StoreFactory::instance());
}

TEST_CASE("store_factory_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<StoreFactory>::value);
    CHECK_FALSE(std::is_copy_assignable<StoreFactory>::value);
}

TEST_CASE("store_factory_has_the_redis_store_registered_by_default")
{
    StoreFactory& factory = StoreFactory::instance();

    CHECK(factory.hasStore("redis"));
    CHECK(factory.createStore("redis") != nullptr);
}

TEST_CASE("store_factory_reports_an_unregistered_name_as_absent")
{
    CHECK_FALSE(StoreFactory::instance().hasStore("no_such_store_for_absent_test"));
}

TEST_CASE("store_factory_returns_null_for_an_unknown_store")
{
    CHECK(StoreFactory::instance().createStore("no_such_store_for_null_test") == nullptr);
}

TEST_CASE("store_factory_registers_and_creates_a_custom_store")
{
    StoreFactory& factory = StoreFactory::instance();
    const std::string name = "stub_custom_store";

    factory.registerStore(name, [] { return std::make_unique<StubStore>(10); });

    CHECK(factory.hasStore(name));

    const auto store = factory.createStore(name);
    REQUIRE(store != nullptr);

    CHECK(store->increment("key", "field", 7));
    CHECK(static_cast<StubStore*>(store.get())->counted() == 70);
}

TEST_CASE("store_factory_creates_a_fresh_instance_on_every_call")
{
    StoreFactory& factory = StoreFactory::instance();
    factory.registerStore("stub_fresh_store", [] { return std::make_unique<StubStore>(1); });

    const auto first = factory.createStore("stub_fresh_store");
    const auto second = factory.createStore("stub_fresh_store");

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first.get() != second.get());
}

TEST_CASE("store_factory_replaces_a_store_registered_under_the_same_name")
{
    StoreFactory& factory = StoreFactory::instance();
    const std::string name = "stub_replaced_store";

    factory.registerStore(name, [] { return std::make_unique<StubStore>(1); });
    factory.registerStore(name, [] { return std::make_unique<StubStore>(2); });

    const auto store = factory.createStore(name);
    REQUIRE(store != nullptr);

    CHECK(store->increment("key", "field", 3));
    CHECK(static_cast<StubStore*>(store.get())->counted() == 6);
}
