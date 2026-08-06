#pragma once

#include "parser/iparser.hpp"
#include "cdr_record.hpp"

#include <optional>
#include <string_view>

namespace cdrp {

class CsvParser : public IParser {
public:
    explicit CsvParser(char separator = '|');

    /**
     * Parses one separated CDR line.
     *
     * @param line: a single record line, without its newline
     * @return the record, or nothing if the line is malformed
     */
    std::optional<CdrRecord> parse(std::string_view line) const override;

private:
    char m_sep;
};

} // namespace cdrp

