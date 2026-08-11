#pragma once

#include "aggregate/delta.hpp"
#include "cdr_record.hpp"

#include <vector>

namespace cdrp {

/**
 * Folds a batch of records into the increments they add up to.
 * Stateless and pure: no I/O, no locks, safe to call from any thread.
 */
class Aggregator {
public:
    /**
     * Accumulates one batch into a Delta, cleared first. A record without a subscriber
     * MSISDN counts nowhere, and a link is mirrored onto the second party.
     *
     * @param batch: the records to fold
     * @param out: the increments, reused across calls to keep its buckets
     */
    void fold(const std::vector<CdrRecord>& batch, Delta& out) const;

private:
    /* Adds what one record carried to both a to b and b to a, nothing when b is 0 */
    static void addLink(LinkMap& links, uint64_t a, uint64_t b, uint64_t dur, uint64_t sms,
                        uint64_t cnt);
};

} // namespace cdrp

