#pragma once

#include "aggregate/delta.hpp"
#include "store/istore.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cdrp {

/**
 * Writes the boards a folded Delta implies into a store.
 * Knows the board keys and what each of them ranks, nothing of the store behind them.
 * Stateless: safe to share between threads if the store is.
 */
class RankWriter {
public:
    /**
     * Constructor.
     *
     * @param store: the store every score is written to
     */
    explicit RankWriter(IStore& store);

    /**
     * Adds every non-zero score of the delta, without flushing: the delta write that
     * follows drains what both of them queued.
     *
     * @param delta: the increments to rank
     * @return false when any score failed to be written
     */
    bool write(const Delta& delta);

private:
    /* Add one score, skipping the write when there is nothing to add */
    bool add(std::string_view board, std::string_view member, uint64_t value);

    /* Appends a decimal number, keeping the capacity the string already has */
    static void appendNumber(std::string& out, uint64_t value);

private:
    IStore& m_store;
};

} // namespace cdrp
