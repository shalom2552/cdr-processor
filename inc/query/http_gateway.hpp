#pragma once

#include "query/query_service.hpp"

#include <memory>

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

    /* Listens on query.port, blocking until stop(). False when the listener never started. */
    bool run();

    /* Stops the listener, so run() returns. Thread-safe. */
    void stop();

private:
    const QueryService& m_service;
    std::unique_ptr<httplib::Server> m_server;
};

} // namespace cdrp
