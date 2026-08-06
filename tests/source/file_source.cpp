#include "doctest.h"
#include "constants.hpp"
#include "parser/csv_parser.hpp"
#include "source/file_source.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

/* Writes text to a fresh file under the temp dir and hands back the path */
class TempFile {
public:
    explicit TempFile(const std::string& text)
        : m_path(std::filesystem::temp_directory_path() / ("cdrp_src_" + std::to_string(++s_counter) + ".cdr"))
    {
        std::ofstream out(m_path, std::ios::binary);
        out << text;
    }

    ~TempFile() { std::filesystem::remove(m_path); }

    std::string path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
    static int s_counter;
};

int TempFile::s_counter = 0;

/* One well formed record, sequence picks it apart from its neighbours */
std::string record(int sequence)
{
    return std::to_string(sequence)
        + "|425020528409010|35-209900-176148-1|MOC|972528409042|12/07/2026|09:57:09|3314|||262040162782277|496221540";
}

/* A file body: the given header line, then count records */
std::string fileWith(std::size_t count, const char* header = nullptr)
{
    std::string text = header ? header : "CDR|csv|" + std::to_string(count) + "\n";
    for (std::size_t i = 0; i < count; ++i) {
        text += record(static_cast<int>(i) + 1) + "\n";
    }
    return text;
}

} // namespace

using namespace cdrp;

TEST_CASE("file_source_reads_every_record_of_a_file")
{
    const TempFile file(fileWith(3));
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);

    REQUIRE(records.size() == 3);
    CHECK(records[0].sequence == 1);
    CHECK(records[2].sequence == 3);
    CHECK(records[0].usageType == UsageType::MOC);
}

TEST_CASE("file_source_reports_done_once_the_file_runs_out")
{
    const TempFile file(fileWith(2));
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);
    CHECK(source.next(records) == ICdrSource::Status::DONE);
    CHECK(records.empty());
}

TEST_CASE("file_source_clears_the_output_between_batches")
{
    const TempFile file(fileWith(2));
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records(5);
    REQUIRE(source.next(records) == ICdrSource::Status::OK);

    CHECK(records.size() == 2);
}

TEST_CASE("file_source_splits_a_long_file_into_batches")
{
    const std::size_t total = kFileBatchSize + 10;
    const TempFile file(fileWith(total));
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);
    CHECK(records.size() == kFileBatchSize);
    CHECK(records.front().sequence == 1);

    REQUIRE(source.next(records) == ICdrSource::Status::OK);
    CHECK(records.size() == 10);
    CHECK(records.back().sequence == static_cast<uint64_t>(total));

    CHECK(source.next(records) == ICdrSource::Status::DONE);
}

TEST_CASE("file_source_reads_a_last_record_without_a_trailing_newline")
{
    const TempFile file("CDR|csv|2\n" + record(1) + "\n" + record(2));
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);

    REQUIRE(records.size() == 2);
    CHECK(records[1].sequence == 2);
}

TEST_CASE("file_source_skips_bad_lines_and_keeps_the_rest")
{
    const TempFile file("CDR|csv|3\n" + record(1) + "\ngarbage line\n" + record(3) + "\n");
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);

    REQUIRE(records.size() == 2);
    CHECK(records[0].sequence == 1);
    CHECK(records[1].sequence == 3);
}

TEST_CASE("file_source_fails_on_a_header_it_does_not_know")
{
    const CsvParser parser;

    for (const char* header : { "", "records:3\n", "CDR|csv\n", "CDR|csv|many\n" }) {
        const TempFile file(fileWith(3, header));
        FileSource source(file.path(), parser);

        std::vector<CdrRecord> records;
        CHECK(source.next(records) == ICdrSource::Status::FAIL);
        CHECK(records.empty());
    }
}

TEST_CASE("file_source_yields_nothing_for_a_header_only_file")
{
    const TempFile file("CDR|csv|0\n");
    const CsvParser parser;
    FileSource source(file.path(), parser);

    std::vector<CdrRecord> records;
    CHECK(source.next(records) == ICdrSource::Status::DONE);
    CHECK(records.empty());
}

TEST_CASE("file_source_fails_on_a_missing_file")
{
    const CsvParser parser;
    FileSource source("/no/such/path/at/all.cdr", parser);

    std::vector<CdrRecord> records;
    CHECK(source.next(records) == ICdrSource::Status::FAIL);
    CHECK(records.empty());
}

TEST_CASE("file_source_is_usable_through_the_icdr_source_interface")
{
    const TempFile file(fileWith(1));
    const CsvParser parser;
    FileSource concrete(file.path(), parser);
    ICdrSource& source = concrete;

    std::vector<CdrRecord> records;
    REQUIRE(source.next(records) == ICdrSource::Status::OK);
    CHECK(records.size() == 1);
}
