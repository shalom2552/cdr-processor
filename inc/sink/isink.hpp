#pragma once

#include "cdr_record.hpp"

#include <vector>

namespace cdrp {

/**
 * A destination for parsed CDR records.
 * Thread-safe: N reader threads call consume() concurrently.
 */
class ISink {
public:
    virtual ~ISink() = default;

    /**
     * Take ownership of a batch of records and store it.
     *
     * @param batch the records to consume
     */
    virtual void consume(std::vector<CdrRecord>& batch) = 0;
};

} // namespace cdrp

