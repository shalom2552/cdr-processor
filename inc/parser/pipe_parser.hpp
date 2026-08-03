#pragma once
#include "parser/iparser.hpp"
#include "cdr_record.hpp"

#include <optional>
#include <string_view>

namespace cdrp {

class PipeParser : public IParser {
public:
    std::optional<CdrRecord> parse(std::string_view line) const override;
};

} // namespace cdrp

