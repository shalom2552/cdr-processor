#pragma once
#include "iparser.hpp"
#include "cdr_record.hpp"

#include <optional>
#include <string_view>

namespace cdrp {

class PipeDelimitedParser : public IParser {
public:
    std::optional<CdrRecord> parse(std::string_view line) const override;
};

} // namespace cdrp

