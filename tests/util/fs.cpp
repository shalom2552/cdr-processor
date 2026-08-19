#include "doctest.h"
#include "util/fs.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

/* A path under the temp dir that nothing has created yet, removed whole afterwards */
class TempDir {
public:
    TempDir()
        : m_path(std::filesystem::temp_directory_path() / ("cdrp_fs_" + std::to_string(++s_counter)))
    {
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    std::string of(const std::string& tail) const
    {
        return (m_path / tail).string();
    }

private:
    std::filesystem::path m_path;
    static int s_counter;
};

int TempDir::s_counter = 0;

} // namespace

using namespace cdrp;

TEST_CASE("basename_of_reads_the_last_component_of_a_path")
{
    CHECK(basename_of("/records/ready/cdr_1.csv") == "cdr_1.csv");
}

TEST_CASE("basename_of_reads_a_path_holding_no_separator")
{
    CHECK(basename_of("cdr_1.csv") == "cdr_1.csv");
}

TEST_CASE("basename_of_reads_nothing_after_a_trailing_separator")
{
    CHECK(basename_of("/records/ready/") == "");
}

TEST_CASE("basename_of_reads_an_empty_path")
{
    CHECK(basename_of("") == "");
}

TEST_CASE("basename_of_reads_a_path_that_is_only_a_separator")
{
    CHECK(basename_of("/") == "");
}

TEST_CASE("ensure_dir_creates_a_nested_path")
{
    const TempDir dir;
    const std::string nested = dir.of("ready/done/archive");

    CHECK(ensure_dir(nested));
    CHECK(std::filesystem::is_directory(nested));
}

TEST_CASE("ensure_dir_succeeds_on_a_path_it_already_created")
{
    const TempDir dir;
    const std::string nested = dir.of("ready/done");

    CHECK(ensure_dir(nested));
    CHECK(ensure_dir(nested));
}

TEST_CASE("ensure_dir_fails_when_the_parent_is_a_file")
{
    const TempDir dir;
    REQUIRE(ensure_dir(dir.of("ready")));
    const std::string file = dir.of("ready/cdr_1.csv");
    std::ofstream(file) << "not a directory\n";

    CHECK_FALSE(ensure_dir(file + "/done"));
}
