#include "query/query_factory.hpp"

#include "store/redis_query.hpp"
#include "logger.hpp"

#include <utility>

constexpr std::string_view kComponent = "QueryFactory";

namespace cdrp {

QueryFactory::QueryFactory()
{
    registerQuery("redis", []() { return std::make_unique<RedisQuery>(); });
}

QueryFactory& QueryFactory::instance()
{
    static QueryFactory instance;
    return instance;
}

void QueryFactory::registerQuery(const std::string& name, Creator creator)
{
    m_queries[name] = std::move(creator);
}

bool QueryFactory::hasQuery(const std::string& name) const
{
    return m_queries.count(name) != 0;
}

std::unique_ptr<IQueryStore> QueryFactory::createQuery(const std::string& name) const
{
    auto it = m_queries.find(name);
    if (it == m_queries.end()) {
        logDebug(kComponent, "no query store for type: " + name);
        return nullptr;
    } else {
        return it->second();
    }
}

} // namespace cdrp
