#include "config.hpp"
#include "logger.hpp"
#include "sink/aggregate_sink.hpp"
#include "store/store_factory.hpp"
#include "ingest/ingestor_factory.hpp"
#include "util/signal_waiter.hpp"

#include <string>
#include <utility>

using namespace cdrp;
static constexpr std::string_view kComponent = "Processor";

int main()
{
    logInfo(kComponent, "starting: '" + cfg.source.mode + "' mode");

    const SignalWaiter signals;

    auto store = StoreFactory::instance().createStore(cfg.store.type);
    if (!store) {
        logError(kComponent, "no store for type: " + cfg.store.type);
        return 1;
    }

    AggregateSink sink(std::move(store));
    auto ingestor = IngestorFactory::instance().createIngestor(cfg.source.mode, sink);
    if (!ingestor) {
        logError(kComponent, "no ingestor for mode: " + cfg.source.mode);
        return 1;
    }

    if (!ingestor->start()) {
        return 1;
    }

    signals.wait();
    logInfo(kComponent, "stopping...");

    ingestor->stop();
    logInfo(kComponent, "totals of this run:" + sink.snapshot().format());
    logInfo(kComponent, "finished");
    return 0;
}

