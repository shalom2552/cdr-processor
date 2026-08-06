#include "config.hpp"
#include "ingest/ingestor_factory.hpp"
#include "logger.hpp"

#include "sink/isink.hpp"

#include <csignal>

using namespace cdrp;

std::atomic<bool> g_stop = false;

class CountingSink : public ISink {
public:
    void consume(std::vector<CdrRecord>& batch) override { m_total.fetch_add(batch.size(), std::memory_order_relaxed); }
    std::atomic<std::size_t> m_total = 0;
};

void run()
{
    CountingSink sink;
    auto ingestor = IngestorFactory::instance().createIngestor(cfg.source.mode, sink);
    if (!ingestor) {
        logError("Main", "no ingestor for mode: " + cfg.source.mode);
        return;
    }

    if (!ingestor->start()) { return; }
    while (!g_stop.load()) { pause(); } // wake on SIGINT

    ingestor->stop();
    logInfo("Main", "consumed " + std::to_string(sink.m_total.load()) + " records");
}

int main()
{
    std::signal(SIGINT, [](int) { g_stop.store(true); logInfo("Main", "stoping..."); });
    logInfo("Main", "starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format + "' format");
    run();
    logInfo("Main", "finished");
    return 0;
}

