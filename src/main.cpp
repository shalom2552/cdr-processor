#include "config.hpp"
#include "logger.hpp"
#include "sink/redis_sink.hpp"
#include "ingest/ingestor_factory.hpp"

#include <csignal>

using namespace cdrp;

std::atomic<bool> g_stop = false;

void run()
{
    RedisSink sink;
    auto ingestor = IngestorFactory::instance().createIngestor(cfg.source.mode, sink);
    if (!ingestor) {
        logError("Main", "no ingestor for mode: " + cfg.source.mode);
        return;
    }

    if (!ingestor->start()) {
        return;
    }
    while (!g_stop.load()) {
        pause(); // wake on SIGINT
    }

    ingestor->stop();
    logInfo("Main", "totals of this run:" + sink.snapshot().format());
}

int main()
{
    std::signal(SIGINT, [](int) { g_stop.store(true); logInfo("Main", "stoping..."); });
    logInfo("Main", "starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format + "' format");
    run();
    logInfo("Main", "finished");
    return 0;
}

