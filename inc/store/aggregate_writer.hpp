#pragma once

#include "aggregate/delta.hpp"
#include "store/istore.hpp"

namespace cdrp {

/**
 * Writes a folded Delta into a store.
 * Knows the key and field names of the aggregates, nothing of the store behind them.
 * Stateless: safe to share between threads if the store is.
 */
class AggregateWriter {
public:
    /**
     * Constructor.
     *
     * @param store: the store every counter is written to
     */
    explicit AggregateWriter(IStore& store);

    /**
     * Writes every non-zero counter of the delta, then flushes the store.
     *
     * @param delta: the increments to write
     * @return false when any counter failed to be written
     */
    bool write(const Delta& delta);

private:
    /* Add one counter, skipping the write when there is nothing to add */
    bool add(std::string_view key, std::string_view field, uint64_t value);

private:
    IStore& m_store;
};

} // namespace cdrp

