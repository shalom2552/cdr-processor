#pragma once

#include "cdr_record.hpp"

#include <cstdint>
#include <string_view>
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
     * @param source: where the batch came from, empty when it is not resumable
     */
    virtual void consume(std::vector<CdrRecord>& batch, std::string_view source) = 0;

    /**
     * How far this source was already consumed, so its reader can skip what landed.
     *
     * @param source: where the records come from
     * @return the highest sequence already consumed, 0 when nothing is kept
     */
    virtual uint64_t resume_at(std::string_view source) { (void)source; return 0; }
};

} // namespace cdrp

