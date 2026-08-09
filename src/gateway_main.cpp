#include "config.hpp"
#include "logger.hpp"
#include "query/http_gateway.hpp"
#include "query/query_factory.hpp"
#include "query/query_service.hpp"
#include "util/signal_waiter.hpp"

#include <csignal>
#include <string>
#include <thread>

static constexpr std::string_view kComponent = "Gateway";

using namespace cdrp;

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
    HttpGateway gateway(service);

    bool listening = false;
    std::thread server([&gateway, &listening] {
        listening = gateway.run();
        if (!listening) {
            raise(SIGTERM);
        }
    });

    logInfo(kComponent, "stopping on signal " + std::to_string(signals.wait()));

    gateway.stop();
    server.join();

    logInfo(kComponent, "finished");
    return listening ? 0 : 1;
}
