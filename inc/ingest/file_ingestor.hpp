#pragma once

#include "ingest/iingestor.hpp"
#include "parser/iparser.hpp"
#include "source/dir_watcher.hpp"
#include "sink/isink.hpp"
#include "util/thread_pool.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace cdrp {

/*
 * Watches the ready directory and turns each delivered file into records.
 * A feeder thread claims files; a thread pool parses them and feeds the sink.
 */
class FileIngestor : public IIngestor {
public:
    FileIngestor(ISink& sink);
    ~FileIngestor() override;

    /* Starts the feeder and workers. False if the format has no parser or the
       watcher failed to init; the ingestor stays stopped. */
    bool start() override;

    /* Stops the feeder, drains the workers and returns once they are joined. */
    void stop() override;

private:
    void feed();
    void process(const std::string& file_path);
    void dispose(const std::string& file_path, bool ok);

private:
    ISink& m_sink;
    std::string m_format;

    std::unique_ptr<IParser> m_parser;
    DirWatcher m_watcher;
    std::thread m_feeder;
    std::unique_ptr<ThreadPool> m_thread_pool;

    std::atomic<bool> m_stop = false;
    bool m_running = false;
};

} // namespace cdrp

