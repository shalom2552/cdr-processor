#include "config.hpp"
#include "logger.hpp"
#include "sink/aggregate_sink.hpp"
#include "store/store_factory.hpp"
#include "ingest/ingestor_factory.hpp"
#include "util/signal_waiter.hpp"

#include <string>
#include <utility>

using namespace cdrp;

void run()
{
    const SignalWaiter signals;

    auto store = StoreFactory::instance().createStore(cfg.store.type);
    if (!store) {
        logError("Main", "no store for type: " + cfg.store.type);
        return;
    }

    AggregateSink sink(std::move(store));
    auto ingestor = IngestorFactory::instance().createIngestor(cfg.source.mode, sink);
    if (!ingestor) {
        logError("Main", "no ingestor for mode: " + cfg.source.mode);
        return;
    }

    if (!ingestor->start()) {
        return;
    }
    logInfo("Main", "stopping on signal " + std::to_string(signals.wait()));

    ingestor->stop();
    logInfo("Main", "totals of this run:" + sink.snapshot().format());
}

int main()
{
    logInfo("Main", "starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format + "' format");
    run();
    logInfo("Main", "finished");
    return 0;
}

