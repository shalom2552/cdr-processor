#include "source/file_source.hpp"
#include "cdr_record.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include <charconv>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace cdrp {

FileSource::FileSource(const std::string& file_path, const IParser& parser)
    : m_map(file_path)
    , m_parser(parser)
    , m_name(std::filesystem::path(file_path).filename().string())
{
    if (!m_map.ok()) {
        logWarn("FileSource", "skipping unreadable file: " + file_path);
        return;
    }
    if (m_map.empty()) {
        logWarn("FileSource", "skipping empty file: " + file_path);
        return;
    }

    const char* data = parse_header(m_map.data(), m_map.size(), m_header);
    if (!data) {
        logWarn("FileSource", "skipping file without a CDR header: " + file_path);
        return;
    }

    m_pos = data;
    m_end = m_map.data() + m_map.size();
    logInfo("FileSource", "reading " + m_name + ": " + m_header.format + ", "
        + std::to_string(m_header.record_count) + " records");
}

const char* FileSource::parse_header(const char* data, std::size_t length, Fileheader& header)
{
    // header "CDR|<format>|<record_count>"
    const char* new_line = static_cast<const char*>(std::memchr(data, '\n', length));
    if (!new_line) {
        return nullptr;
    }

    std::string_view line(data, new_line - data);
    if (line.substr(0, 4) != "CDR|") {
        return nullptr;
    }

    std::size_t bar = line.find('|', 4);
    if (bar == std::string_view::npos) {
        return nullptr;
    }

    std::string_view count = line.substr(bar + 1);
    if (std::from_chars(count.data(), count.data() + count.size(), header.record_count).ec != std::errc()) {
        return nullptr;
    }

    header.format = line.substr(4, bar - 4);
    return new_line + 1;
}

FileSource::Status FileSource::next(std::vector<CdrRecord>& out)
{
    out.clear();

    while (m_pos < m_end && out.size() < kBatchSize) {
        const char* new_line = static_cast<const char*>(std::memchr(m_pos, '\n', m_end - m_pos));
        const char* end_line = new_line ? new_line : m_end;

        std::string_view line(m_pos, end_line - m_pos);
        m_pos = new_line ? new_line + 1 : m_end;

        if (auto record = m_parser.parse(line)) {
            out.push_back(*record);
        } else {
            ++m_rejected;
        }
    }

    if (out.empty()) {
        log_summary();
        return Status::DONE;
    }

    m_parsed += out.size();
    logDebug("FileSource", "batch of " + std::to_string(out.size()) + " records");
    return Status::OK;
}

void FileSource::log_summary()
{
    if (m_summed || !m_end) { // !m_end: file never opened
        return;
    }
    m_summed = true;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_started);
    logInfo("FileSource", "done " + m_name + ": " + std::to_string(m_parsed) + " parsed, "
        + std::to_string(m_rejected) + " rejected, " + std::to_string(elapsed.count()) + "ms");
}


} // namespace cdrp

