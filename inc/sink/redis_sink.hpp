#pragma once

#include "sink/isink.hpp"
#include "cdr_record.hpp"
#include "store/aggregate_writer.hpp"
#include "aggregate/aggregator.hpp"
#include "store/redis_store.hpp"

#include <atomic>
#include <cstddef>
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
     * The records taken so far, counted whether or not they reached Redis.
     *
     * @return the number of records passed to consume()
     */
    std::size_t total() const;

private:
    Aggregator m_aggregator;
    RedisStore m_store;
    AggregateWriter m_writer;

    std::atomic<std::size_t> m_total = 0;
};

} // namespace cdrp

