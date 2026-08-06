#include "doctest.h"
#include "ingest/file_ingestor.hpp"
#include "ingest/iingestor.hpp"
#include "sink/isink.hpp"
#include "config.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace std::chrono_literals;

/* Every wait here is bounded, so a stuck ingestor fails the suite instead of hanging it */
constexpr auto kTimeout = 5s;

/* A sink that stores what it consumes and lets a test wait for a record count */
class RecordingSink : public cdrp::ISink {
public:
    void consume(std::vector<cdrp::CdrRecord>& batch) override
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& record : batch) {
                m_sequences.insert(record.sequence);
            }
            m_count += batch.size();
        }
        m_cv.notify_all();
    }

    /* Returns false if the count was still below target when the timeout expired */
    bool waitFor(std::size_t target)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kTimeout, [this, target] { return m_count >= target; });
    }

    std::size_t count()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

    bool has(uint64_t sequence)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_sequences.count(sequence) != 0;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::set<uint64_t> m_sequences;
    std::size_t m_count = 0;
};

/*
 * Owns the configured ready/process/done/failed dirs and a staging dir, and removes
 * every file it delivers when the test ends. The ingestor reads these paths from cfg,
 * so the test can only use the real configured directories.
 */
class IngestFixture {
public:
    IngestFixture()
    {
        for (const std::string& dir : { cdrp::cfg.file.ready_dir, cdrp::cfg.file.process_dir,
                                        cdrp::cfg.file.done_dir, cdrp::cfg.file.fail_dir }) {
            fs::create_directories(dir);
        }
        fs::create_directories(m_staging);
    }

    ~IngestFixture()
    {
        std::error_code ec;
        for (const std::string& name : m_delivered) {
            for (const std::string& dir : { cdrp::cfg.file.ready_dir, cdrp::cfg.file.process_dir,
                                            cdrp::cfg.file.done_dir, cdrp::cfg.file.fail_dir }) {
                fs::remove(fs::path(dir) / name, ec);
            }
        }
        fs::remove_all(m_staging, ec);
    }

    /* Writes a file into ready_dir directly, before the ingestor is constructed */
    void place(const std::string& name, const std::string& text)
    {
        m_delivered.insert(name);
        std::ofstream out(fs::path(cdrp::cfg.file.ready_dir) / name, std::ios::binary);
        out << text;
    }

    /* Delivers a file by rename, the way a real sender does, after the ingestor started */
    void deliver(const std::string& name, const std::string& text)
    {
        m_delivered.insert(name);
        const fs::path staged = m_staging / name;
        std::ofstream out(staged, std::ios::binary);
        out << text;
        out.close();
        fs::rename(staged, fs::path(cdrp::cfg.file.ready_dir) / name);
    }

    bool inDone(const std::string& name) const
    {
        return fs::exists(fs::path(cdrp::cfg.file.done_dir) / name);
    }

private:
    fs::path m_staging = fs::path(".cdrp_ingest_test_stage");
    std::set<std::string> m_delivered;
};

/* One well formed csv record, sequence sets it apart from its neighbours */
std::string record(uint64_t sequence)
{
    return std::to_string(sequence)
        + "|425020528409010|35-209900-176148-1|MOC|972528409042|12/07/2026|09:57:09|3314|||262040162782277|496221540";
}

/* A file body: the csv header line, then records base+1 .. base+count */
std::string fileWith(uint64_t base, std::size_t count)
{
    std::string text = "CDR|csv|" + std::to_string(count) + "\n";
    for (std::size_t i = 0; i < count; ++i) {
        text += record(base + i + 1) + "\n";
    }
    return text;
}

} // namespace

using namespace cdrp;

TEST_CASE("file_ingestor_is_neither_copyable_nor_copy_assignable")
{
    CHECK_FALSE(std::is_copy_constructible<FileIngestor>::value);
    CHECK_FALSE(std::is_copy_assignable<FileIngestor>::value);
}

TEST_CASE("file_ingestor_constructs_and_destructs_without_starting")
{
    RecordingSink sink;
    FileIngestor ingestor(sink);

    CHECK(sink.count() == 0);
}

TEST_CASE("file_ingestor_stop_is_safe_without_a_start")
{
    RecordingSink sink;
    FileIngestor ingestor(sink);

    ingestor.stop();

    CHECK(sink.count() == 0);
}

TEST_CASE("file_ingestor_start_returns_true_and_is_idempotent")
{
    IngestFixture dirs;
    RecordingSink sink;
    FileIngestor ingestor(sink);

    CHECK(ingestor.start());
    CHECK(ingestor.start());

    ingestor.stop();
}

TEST_CASE("file_ingestor_is_usable_through_the_iingestor_interface")
{
    IngestFixture dirs;
    RecordingSink sink;
    FileIngestor concrete(sink);
    IIngestor& ingestor = concrete;

    CHECK(ingestor.start());
    ingestor.stop();
}

TEST_CASE("file_ingestor_processes_a_file_delivered_after_it_starts")
{
    IngestFixture dirs;
    RecordingSink sink;
    FileIngestor ingestor(sink);

    REQUIRE(ingestor.start());
    dirs.deliver("delivered.cdr", fileWith(1000, 3));

    REQUIRE(sink.waitFor(3));
    ingestor.stop();

    CHECK(sink.has(1001));
    CHECK(sink.has(1002));
    CHECK(sink.has(1003));
    CHECK(dirs.inDone("delivered.cdr"));
}

TEST_CASE("file_ingestor_processes_a_file_already_present_before_it_starts")
{
    IngestFixture dirs;
    dirs.place("present.cdr", fileWith(2000, 2));

    RecordingSink sink;
    FileIngestor ingestor(sink);

    REQUIRE(ingestor.start());
    REQUIRE(sink.waitFor(2));
    ingestor.stop();

    CHECK(sink.has(2001));
    CHECK(sink.has(2002));
    CHECK(dirs.inDone("present.cdr"));
}

TEST_CASE("file_ingestor_processes_several_files")
{
    IngestFixture dirs;
    RecordingSink sink;
    FileIngestor ingestor(sink);

    REQUIRE(ingestor.start());
    dirs.deliver("first.cdr", fileWith(3000, 4));
    dirs.deliver("second.cdr", fileWith(4000, 4));

    REQUIRE(sink.waitFor(8));
    ingestor.stop();

    CHECK(sink.has(3001));
    CHECK(sink.has(4004));
    CHECK(dirs.inDone("first.cdr"));
    CHECK(dirs.inDone("second.cdr"));
}

TEST_CASE("file_ingestor_disposes_a_header_only_file_without_emitting_records")
{
    IngestFixture dirs;
    RecordingSink sink;
    FileIngestor ingestor(sink);

    REQUIRE(ingestor.start());
    dirs.deliver("empty.cdr", "CDR|csv|0\n");

    /* No records to wait on, so poll the disposition under the same bound */
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (!dirs.inDone("empty.cdr") && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    ingestor.stop();

    CHECK(dirs.inDone("empty.cdr"));
    CHECK(sink.count() == 0);
}
