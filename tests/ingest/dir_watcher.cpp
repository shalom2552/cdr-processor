#include "doctest.h"
#include "ingest/dir_watcher.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <type_traits>

namespace fs = std::filesystem;

namespace {

/* How long a test waits for the watcher before calling it stuck */
constexpr auto kWait = std::chrono::milliseconds(2000);

/* How long a test waits to be convinced the watcher has nothing to give */
constexpr auto kQuietWait = std::chrono::milliseconds(300);

/* A source, a target and a staging dir, all empty, all gone at the end of the test */
class TempDirs {
public:
    TempDirs()
        : m_root(fs::temp_directory_path() / ("cdrp_watch_" + std::to_string(++s_counter)))
    {
        fs::create_directories(source());
        fs::create_directories(target());
        fs::create_directories(staging());
    }

    ~TempDirs()
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    fs::path source() const { return m_root / "in"; }
    fs::path target() const { return m_root / "work"; }
    fs::path staging() const { return m_root / "stage"; }

private:
    fs::path m_root;
    static int s_counter;
};

int TempDirs::s_counter = 0;

/* What next_file said, answered stays false when the watcher never came back in time */
struct Claim {
    bool answered = false;
    bool ok = false;
    std::string path;
};

/* Writes a file straight into dir, no rename */
void write_file(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/* Delivers a file the way the sender does: write elsewhere, then rename into dir */
void deliver(const TempDirs& dirs, const fs::path& dir, const std::string& name, const std::string& text)
{
    const fs::path staged = dirs.staging() / name;
    write_file(staged, text);
    fs::rename(staged, dir / name);
}

/* Reads a whole file back */
std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/*
 * Calls next_file on its own thread so a watcher that never returns fails the
 * suite instead of hanging it. The watcher is shared so a stuck thread cannot
 * outlive it.
 */
Claim next_within(const std::shared_ptr<cdrp::DirWatcher>& watcher, std::chrono::milliseconds wait = kWait)
{
    auto slot = std::make_shared<std::promise<Claim>>();
    auto answer = slot->get_future();

    std::thread([watcher, slot]() {
        Claim claim;
        claim.answered = true;
        claim.ok = watcher->next_file(claim.path);
        slot->set_value(claim);
    }).detach();

    if (answer.wait_for(wait) != std::future_status::ready) {
        return Claim{};
    }
    return answer.get();
}

} // namespace

using namespace cdrp;

TEST_CASE("dir_watcher_is_ok_for_an_existing_directory")
{
    const TempDirs dirs;
    const DirWatcher watcher(dirs.source().string(), dirs.target().string());

    CHECK(watcher.ok());
}

TEST_CASE("dir_watcher_creates_a_missing_directory")
{
    const TempDirs dirs;
    const fs::path missing = dirs.source() / "nowhere";
    const DirWatcher watcher(missing.string(), dirs.target().string());

    CHECK(watcher.ok());
    CHECK(fs::is_directory(missing));
}

TEST_CASE("dir_watcher_is_not_ok_for_a_directory_it_cannot_create")
{
    const TempDirs dirs;
    write_file(dirs.source() / "blocker", "not a directory\n");
    const DirWatcher watcher((dirs.source() / "blocker" / "in").string(), dirs.target().string());

    CHECK_FALSE(watcher.ok());
}

TEST_CASE("dir_watcher_gives_up_instead_of_blocking_when_it_is_not_ok")
{
    const TempDirs dirs;
    write_file(dirs.source() / "blocker", "not a directory\n");
    auto watcher = std::make_shared<DirWatcher>(
        (dirs.source() / "blocker" / "in").string(), dirs.target().string());

    const Claim claim = next_within(watcher);

    REQUIRE(claim.answered);
    CHECK_FALSE(claim.ok);
}

TEST_CASE("dir_watcher_claims_a_file_left_in_the_source_dir")
{
    const TempDirs dirs;
    write_file(dirs.source() / "old.cdr", "CDR|csv|0\n");

    auto watcher = std::make_shared<DirWatcher>(dirs.source().string(), dirs.target().string());
    const Claim claim = next_within(watcher);

    REQUIRE(claim.answered);
    REQUIRE(claim.ok);
    CHECK(fs::path(claim.path).parent_path() == dirs.target());
    CHECK(fs::exists(claim.path));
    CHECK(read_file(claim.path) == "CDR|csv|0\n");
    CHECK_FALSE(fs::exists(dirs.source() / "old.cdr"));
}

TEST_CASE("dir_watcher_claims_a_file_delivered_after_it_starts")
{
    const TempDirs dirs;
    auto watcher = std::make_shared<DirWatcher>(dirs.source().string(), dirs.target().string());
    REQUIRE(watcher->ok());

    deliver(dirs, dirs.source(), "new.cdr", "CDR|csv|0\n");
    const Claim claim = next_within(watcher);

    REQUIRE(claim.answered);
    REQUIRE(claim.ok);
    CHECK(fs::path(claim.path).filename() == "new.cdr");
    CHECK(fs::path(claim.path).parent_path() == dirs.target());
    CHECK_FALSE(fs::exists(dirs.source() / "new.cdr"));
}

TEST_CASE("dir_watcher_picks_up_files_left_in_the_target_dir")
{
    const TempDirs dirs;
    write_file(dirs.target() / "half_done.cdr", "CDR|csv|0\n");

    auto watcher = std::make_shared<DirWatcher>(dirs.source().string(), dirs.target().string());
    const Claim claim = next_within(watcher);

    REQUIRE(claim.answered);
    REQUIRE(claim.ok);
    CHECK(claim.path == (dirs.target() / "half_done.cdr").string());
    CHECK(fs::exists(claim.path));
}

TEST_CASE("dir_watcher_hands_out_every_file_once")
{
    const TempDirs dirs;
    write_file(dirs.source() / "a.cdr", "a");
    write_file(dirs.source() / "b.cdr", "b");

    auto watcher = std::make_shared<DirWatcher>(dirs.source().string(), dirs.target().string());
    deliver(dirs, dirs.source(), "c.cdr", "c");

    std::set<std::string> names;
    for (int i = 0; i < 3; ++i) {
        const Claim claim = next_within(watcher);
        REQUIRE(claim.answered);
        REQUIRE(claim.ok);
        names.insert(fs::path(claim.path).filename().string());
    }

    CHECK(names == std::set<std::string>{ "a.cdr", "b.cdr", "c.cdr" });
    CHECK(fs::is_empty(dirs.source()));
}

TEST_CASE("dir_watcher_waits_while_the_directory_stays_empty")
{
    const TempDirs dirs;
    auto watcher = std::make_shared<DirWatcher>(dirs.source().string(), dirs.target().string());
    REQUIRE(watcher->ok());

    const Claim claim = next_within(watcher, kQuietWait);

    CHECK_FALSE(claim.answered);
}

TEST_CASE("dir_watcher_cannot_be_copied")
{
    CHECK_FALSE(std::is_copy_constructible<DirWatcher>::value);
    CHECK_FALSE(std::is_copy_assignable<DirWatcher>::value);
}
