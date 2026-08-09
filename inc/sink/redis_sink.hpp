#pragma once

#include "sink/isink.hpp"
#include "cdr_record.hpp"
#include "store/aggregate_writer.hpp"
#include "aggregate/aggregator.hpp"
#include "aggregate/totals.hpp"
#include "store/redis_store.hpp"

#include <vector>

namespace cdrp {

/**
 * Folds every batch into its increments and writes them to Redis.
 * Thread-safe: the fold uses per-thread state and the store one connection per thread.
 */
class RedisSink : public ISink {
public:
    /* Constructor, hands the store to the writer that fills it */
    RedisSink();

    /**
     * Folds one batch, writes it, and counts its records. A batch that did not fully
     * land is logged and dropped.
     *
     * @param batch: the records to aggregate
     */
    void consume(std::vector<CdrRecord>& batch) override;

    /**
     * What this run took, counted whether or not it reached Redis. The hash in Redis
     * holds every run since it was last cleared, this holds one.
     *
     * @return the counters of the records passed to consume()
     */
    Totals snapshot() const;

private:
    Aggregator m_aggregator;
    RedisStore m_store;
    AggregateWriter m_writer;

    RunTotals m_totals;
};

} // namespace cdrp

