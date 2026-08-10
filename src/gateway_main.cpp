#include "config.hpp"
#include "logger.hpp"
#include "query/http_gateway.hpp"
#include "query/query_factory.hpp"
#include "query/query_service.hpp"
#include "util/signal_waiter.hpp"

#include <string>

using namespace cdrp;
static constexpr std::string_view kComponent = "Gateway";

int main()
{
    logInfo(kComponent, "starting: query gateway on port " + std::to_string(cfg.query.port));

    const SignalWaiter signals;

    auto store = QueryFactory::instance().createQuery(cfg.store.type);
    if (!store) {
        logError(kComponent, "no query store for type: " + cfg.store.type);
        return 1;
    }

    QueryService service(*store);
    HttpGateway gateway(service, cfg.query.port, cfg.query.host);

    if (!gateway.start()) {
        return 1;
    }
    logInfo(kComponent, "stopping on signal " + std::to_string(signals.wait()));

    gateway.stop();

    logInfo(kComponent, "finished");
    return 0;
}
