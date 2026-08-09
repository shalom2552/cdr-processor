#pragma once

#include "sink/isink.hpp"
#include "cdr_record.hpp"
#include "aggregate/aggregate_writer.hpp"
#include "aggregate/aggregator.hpp"
#include "aggregate/totals.hpp"
#include "store/istore.hpp"

#include <memory>
#include <vector>

namespace cdrp {

/**
 * Folds every batch into its increments and writes them through the store it was built with.
 * Holds an Aggregator, that store, and an AggregateWriter over it.
 * Thread-safe: the fold uses per-thread state and the store one connection per thread.
 */
class AggregateSink : public ISink {
public:
    /**
     * Constructor, hands the store to the writer that fills it.
     *
     * @param store: the store every counter is written to
     */
    explicit AggregateSink(std::unique_ptr<IStore> store);

    /**
     * Folds one batch, writes it, and counts its records. A batch that did not fully
     * land is logged and dropped.
     *
     * @param batch: the records to aggregate
     */
    void consume(std::vector<CdrRecord>& batch) override;

    /**
     * What this run took, counted whether or not it reached the store. The hash in the
     * store holds every run since it was last cleared, this holds one.
     *
     * @return the counters of the records passed to consume()
     */
    Totals snapshot() const;

private:
    Aggregator m_aggregator;
    std::unique_ptr<IStore> m_store;
    AggregateWriter m_writer;

    RunTotals m_totals;
};

} // namespace cdrp

