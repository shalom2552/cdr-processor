#include "store/store_factory.hpp"

#include "store/redis_store.hpp"
#include "logger.hpp"

#include <utility>

constexpr std::string_view kComponent = "StoreFactory";

namespace cdrp {

StoreFactory::StoreFactory()
{
    registerStore("redis", []() { return std::make_unique<RedisStore>(); });
}

StoreFactory& StoreFactory::instance()
{
    static StoreFactory instance;
    return instance;
}

void StoreFactory::registerStore(const std::string& name, Creator creator)
{
    m_stores[name] = std::move(creator);
}

bool StoreFactory::hasStore(const std::string& name) const
{
    return m_stores.count(name) != 0;
}

std::unique_ptr<IStore> StoreFactory::createStore(const std::string& name) const
{
    auto it = m_stores.find(name);
    if (it == m_stores.end()) {
        logDebug(kComponent, "no store for type: " + name);
        return nullptr;
    } else {
        return it->second();
    }
}

} // namespace cdrp

