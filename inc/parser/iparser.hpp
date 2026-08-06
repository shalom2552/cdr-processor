#pragma once

#include "cdr_record.hpp"

#include <optional>
#include <string_view>

namespace cdrp {

/* Interface class for parsing records */
class IParser {
public:
    virtual ~IParser() = default;
    virtual std::optional<CdrRecord> parse(std::string_view line) const = 0;
};

} // namespace cdrp

