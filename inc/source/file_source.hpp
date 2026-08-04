#pragma once

#include "parser/iparser.hpp"
#include "source/icdr_source.hpp"
#include "util/mapped_file.hpp"
#include "cdr_record.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace cdrp {

// CDR records file header structure.
struct Fileheader {
    std::string format;
    std::size_t record_count;
};

class FileSource : public ICdrSource {
public:
    FileSource(const std::string& path, const IParser& parser);

    Status next(std::vector<CdrRecord>& out) override;

private:
    /* Parse the file header and return pointer to the first record */
    const char* parse_header(const char* data, std::size_t length, Fileheader& header);

private:
    /* Log the parsed/rejected counts and elapsed time, once per file */
    void log_summary();

private:
    MappedFile m_map;
    const IParser& m_parser;
    Fileheader m_header;
    const char* m_pos = nullptr;
    const char* m_end = nullptr;

    std::string m_name;
    std::size_t m_parsed = 0;
    std::size_t m_rejected = 0;
    bool m_summed = false;
    std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};

} // namespace cdrp

