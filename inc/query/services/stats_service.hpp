#pragma once

#include "query/iquery_store.hpp"
#include "query/result.hpp"

namespace cdrp {

/**
 * Answers what the store itself holds, rather than what is in it.
 * Knows the totals hash and the shape of the responses, nothing of HTTP.
 * Stateless: safe to share between threads if the store is.
 */
class StatsService {
public:
    /**
     * Constructor.
     * @param store: the store every query is read from
     */
    explicit StatsService(const IQueryStore& store);

    /* Whether the store answers, its key count and the path bounds, always 200 */
    Result health() const;

    /* The store's lifetime counters, 0 for every one it does not hold */
    Result totals() const;

private:
    const IQueryStore& m_store;
};

} // namespace cdrp
