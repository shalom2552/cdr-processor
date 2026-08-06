#include "doctest.h"
#include "util/mapped_file.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

/* Writes text to a fresh file under the temp dir and hands back the path */
class TempFile {
public:
    explicit TempFile(std::string_view text)
        : m_path(std::filesystem::temp_directory_path() / ("cdrp_" + std::to_string(++s_counter) + ".tmp"))
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

} // namespace

using namespace cdrp;

TEST_CASE("mapped_file_is_not_copyable")
{
    CHECK_FALSE(std::is_copy_constructible<MappedFile>::value);
    CHECK_FALSE(std::is_copy_assignable<MappedFile>::value);
}

TEST_CASE("mapped_file_maps_a_file")
{
    const std::string text = "2519|425020528409010|MOC\n2520|425020528409010|D\n";
    const TempFile file(text);

    const MappedFile mapped(file.path());

    REQUIRE(mapped.ok());
    CHECK_FALSE(mapped.empty());
    CHECK(mapped.size() == text.size());
    REQUIRE(mapped.data() != nullptr);
    CHECK(std::string_view(mapped.data(), mapped.size()) == text);
}

TEST_CASE("mapped_file_maps_an_empty_file")
{
    const TempFile file("");

    const MappedFile mapped(file.path());

    CHECK(mapped.empty());
    CHECK(mapped.size() == 0);
}

TEST_CASE("mapped_file_fails_on_a_missing_file")
{
    const MappedFile mapped("/no/such/path/at/all.cdr");
    CHECK_FALSE(mapped.ok());
    CHECK(mapped.empty());
    CHECK(mapped.size() == 0);
}

TEST_CASE("mapped_file_reads_the_same_file_twice")
{
    const TempFile file("line one\nline two\n");

    const MappedFile first(file.path());
    const MappedFile second(file.path());

    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.size() == second.size());
    CHECK(std::string_view(first.data(), first.size()) == std::string_view(second.data(), second.size()));
}

TEST_CASE("mapped_file_holds_a_file_larger_than_a_page")
{
    const std::string text(200000, 'x');
    const TempFile file(text);

    const MappedFile mapped(file.path());

    REQUIRE(mapped.ok());
    CHECK(mapped.size() == text.size());
    CHECK(mapped.data()[text.size() - 1] == 'x');
}
