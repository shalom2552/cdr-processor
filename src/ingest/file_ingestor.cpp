#include "ingest/file_ingestor.hpp"
#include "source/file_source.hpp"
#include "parser/parser_factory.hpp"
#include "cdr_record.hpp"
#include "config.hpp"
#include "logger.hpp"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string basename_of(const std::string& path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

} // namespace

namespace cdrp {

FileIngestor::FileIngestor(ISink& sink)
    : m_sink(sink)
    , m_format(cfg.source.format)
    , m_parser(ParserFactory::instance().createParser(m_format))
    , m_watcher(cfg.file.ready_dir, cfg.file.process_dir)
    , m_thread_pool(std::make_unique<ThreadPool>(cfg.file.readers, 2 * cfg.file.readers))
{
}

FileIngestor::~FileIngestor()
{
    stop();
}

bool FileIngestor::start()
{
    if (m_running) {
        return true;
    }

    if (!m_parser) {
        logError("FileIngestor", "no parser for format: " + m_format);
        return false;
    }

    if (!m_watcher.ok()) {
        logError("FileIngestor", "watcher init failed");
        return false;
    }

    m_feeder = std::thread(&FileIngestor::feed, this);
    m_running = true;

    logInfo("FileIngestor", "started");
    return true;
}

void FileIngestor::stop()
{
    if (!m_running) {
        return;
    }

    m_stop.store(true, std::memory_order_relaxed);
    m_watcher.wake();       // interupt the watcher thread
    if (m_feeder.joinable()) {
        m_feeder.join();
    }
    m_thread_pool.reset();  // drains and join

    m_running = false;
    logInfo("FileIngestor", "stopped");
}

void FileIngestor::feed()
{
    std::string path;
    while (!m_stop.load(std::memory_order_relaxed)) {
        if (!m_watcher.next_file(path)) {
            if (!m_stop.load(std::memory_order_relaxed)) {
                logError("FileIngestor", "watcher stopped, ingestion halted");
            }
            break;
        }
        logDebug("FileIngestor", "claimed " + path);
        m_thread_pool->submit([this, path]() { process(path); });
    }
}

void FileIngestor::process(const std::string& file_path)
{
    FileSource src(file_path, *m_parser);

    std::vector<CdrRecord> batch;
    FileSource::Status st;

    while ((st = src.next(batch)) == FileSource::Status::OK) {
        m_sink.consume(batch);
    }

    dispose(file_path, st == FileSource::Status::DONE);
}

void FileIngestor::dispose(const std::string& file_path, bool ok)
{
    const std::string dest = (ok ? cfg.file.done_dir : cfg.file.fail_dir) + basename_of(file_path);

    if (!ok) {
        logWarn("FileIngestor", "file failed, routed to failed dir: " + basename_of(file_path));
    }

    if (std::rename(file_path.c_str(), dest.c_str()) != 0) {
        logWarn("FileIngestor", "rename failed: " + file_path + " -> " + dest);
    }
}

} // namespace cdrp

