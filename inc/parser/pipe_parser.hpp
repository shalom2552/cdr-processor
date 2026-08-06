#pragma once

#include "parser/iparser.hpp"
#include "cdr_record.hpp"

#include <optional>
#include <string_view>

namespace cdrp {

class PipeParser : public IParser {
public:
    /**
     * Parses one pipe separated CDR line.
     *
     * @param line: a single record line, without its newline
     * @return the record, or nothing if the line is malformed
     */
    std::optional<CdrRecord> parse(std::string_view line) const override;
};

} // namespace cdrp

