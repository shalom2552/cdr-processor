#pragma once

#include "query/query_service.hpp"

#include <memory>
#include <thread>

namespace httplib { class Server; }

namespace cdrp {

/**
 * Serves the query API over HTTP.
 * One thread per connection, capped by query.concurrency; each handler reads the
 * store on its own connection, so nothing is locked.
 */
class HttpGateway {
public:
    /**
     * Constructor, builds the server and binds the routes.
     *
     * @param service: the service every request is answered from
     */
    explicit HttpGateway(const QueryService& service);
    ~HttpGateway();

    HttpGateway(const HttpGateway&) = delete;
    HttpGateway& operator=(const HttpGateway&) = delete;

    /* Binds query.port and serves it on a thread of its own. False when the port
       could not be bound; the gateway stays stopped. */
    bool start();

    /* Stops the listener and returns once its thread is joined. */
    void stop();

private:
    const QueryService& m_service;
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_listener;
    bool m_running = false;
};

} // namespace cdrp
