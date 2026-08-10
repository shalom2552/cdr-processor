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
     * land is logged and dropped. A named source also has its progress written, in the
     * same transaction as the counters it belongs with.
     *
     * @param batch: the records to aggregate
     * @param source: where the batch came from, empty when it is not resumable
     */
    void consume(std::vector<CdrRecord>& batch, std::string_view source) override;

    /**
     * Asks the store how far this source was already applied.
     *
     * @param source: where the records come from
     * @return the highest sequence already applied, 0 when the source is unseen
     */
    uint64_t resume_at(std::string_view source) override;

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

