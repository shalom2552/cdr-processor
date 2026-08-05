#include "config.hpp"
#include "logger.hpp"
using namespace cdrp;

#include <csignal>

#include "sink/isink.hpp"
#include "ingest/file_ingestor.hpp"

class CountingSink : public ISink {
public:
    void consume(std::vector<CdrRecord>& batch) override { m_total.fetch_add(batch.size(), std::memory_order_relaxed); }
    std::atomic<std::size_t> m_total = 0;
};

std::atomic<bool> g_stop = false;

int main()
{
    std::signal(SIGINT, [](int) { g_stop.store(true); });
    logInfo("Main", "starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format + "' format");

    CountingSink sink;
    FileIngestor ingestor(sink);

    if (!ingestor.start()) { return 1; }
    while (!g_stop.load()) { pause(); } // wake on SIGINT

    ingestor.stop();
    logInfo("Main", "consumed " + std::to_string(sink.m_total.load()) + " records");
    logInfo("Main", "finished");
    return 0;
}

