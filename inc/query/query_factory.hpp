#pragma once

#include "store/iquery_store.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cdrp {

/*
 * Maps a store type to a query store. Holds the known query stores and builds one
 * on demand. Single shared instance; register at startup, create per run.
 */
class QueryFactory {
public:
    using Creator = std::function<std::unique_ptr<IQueryStore>()>;

    static QueryFactory& instance();
    ~QueryFactory() = default;

    QueryFactory(const QueryFactory&) = delete;
    QueryFactory& operator=(const QueryFactory&) = delete;

    /* Register a query store under name, replacing any query store already under it. */
    void registerQuery(const std::string& name, Creator creator);

    /* True if a query store is registered under name. */
    bool hasQuery(const std::string& name) const;

    /* Build a new query store for name, or nullptr if none is registered. */
    std::unique_ptr<IQueryStore> createQuery(const std::string& name) const;

private:
    QueryFactory();

private:
    std::unordered_map<std::string, Creator> m_queries;
};

} // namespace cdrp
