#pragma once

#include <vector>
#include "cdr_record.hpp"

namespace cdrp {

class ICdrSource {
public:
    enum class Status {
        OK,
        DONE,
        FAIL,
    };

    virtual ~ICdrSource() = default;

    /**
     * Get next batch of records from source.
     *
     * @param out vector to store records in
     * @return status of the operation
     */
    virtual Status next(std::vector<CdrRecord>& out) = 0;
};

} // namespace cdrp

