#include "query/http_gateway.hpp"

#include "query/query_params.hpp"
#include "util/json.hpp"
#include "config.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include "httplib.h"
#include <string>
#include <utility>

static constexpr std::string_view kComponent = "HttpGateway";

namespace cdrp {

/* Send one result as the response */
static void send(httplib::Response& res, Result result)
{
    res.status = result.status;
    res.set_content(std::move(result.body), "application/json");
}

HttpGateway::HttpGateway(const IQueryStore& store, const int port, std::string host)
    : m_service(store)
    , m_paths(store)
    , m_stats(store)
    , m_ranks(store)
    , m_port(port)
    , m_host(std::move(host))
    , m_server(std::make_unique<httplib::Server>())
{
    m_server->new_task_queue = [] {
        return new httplib::ThreadPool(cfg.query.concurrency);
    };

    // set reuse address option, not reuse port
    m_server->set_socket_options([](const socket_t sock) {
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
    });

    registerQueryRoutes();
    registerStatsRoutes();
    registerRankRoutes();

    m_server->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            res.set_content(Json::error("no such route"), "application/json");
        }
    });

    m_server->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr) {
        logWarn(kComponent, "handler threw on " + req.path);
        res.status = 500;
        res.set_content(Json::error("internal error"), "application/json");
    });

    m_server->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        logInfo(kComponent, req.method + " " + req.path + " -> " + std::to_string(res.status));
    });
}

void HttpGateway::registerQueryRoutes()
{
    m_server->Get(R"(/query/msisdn/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        send(res, m_service.msisdn(req.matches[1].str()));
    });

    m_server->Get(R"(/query/operator/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        send(res, m_service.op(req.matches[1].str()));
    });

    m_server->Get(R"(/query/link/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        QueryParams params;
        const Result refused = parseParams(req, kPeerLimit, kPeerLimitMax, params);
        if (refused.status != 200) {
            send(res, refused);
            return;
        }
        send(res, m_service.peers(req.matches[1].str(), params));
    });

    m_server->Get(R"(/query/link/(\d+)/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        send(res, m_service.link(req.matches[1].str(), req.matches[2].str()));
    });

    m_server->Get(R"(/query/path/(\d+)/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        QueryParams params;
        const Result refused = parseParams(req, kPeerLimit, kPeerLimitMax, params);
        if (refused.status != 200) {
            send(res, refused);
            return;
        }
        send(res, m_paths.path(req.matches[1].str(), req.matches[2].str(), params.weights));
    });
}

void HttpGateway::registerStatsRoutes()
{
    m_server->Get("/query/health", [this](const httplib::Request&, httplib::Response& res) {
        send(res, m_stats.health());
    });

    m_server->Get("/query/totals", [this](const httplib::Request&, httplib::Response& res) {
        send(res, m_stats.totals());
    });
}

void HttpGateway::registerRankRoutes()
{
    m_server->Get(R"(/query/top/([a-z-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        QueryParams params;
        const Result refused = parseParams(req, kTopLimit, kTopLimitMax, params);
        if (refused.status != 200) {
            send(res, refused);
            return;
        }
        send(res, m_ranks.top(req.matches[1].str(), params));
    });
}

HttpGateway::~HttpGateway()
{
    stop();
}

bool HttpGateway::start()
{
    if (m_running) {
        return true;
    }

    const int bound = m_port == 0 ? m_server->bind_to_any_port(m_host)
                                  : m_server->bind_to_port(m_host, m_port) ? m_port : -1;
    if (bound <= 0) {
        logError(kComponent, "could not bind " + m_host + ":" + std::to_string(m_port));
        return false;
    }

    m_port = bound;
    logInfo(kComponent, "starting on " + m_host + ":" + std::to_string(m_port));

    m_listener = std::thread([this] { m_server->listen_after_bind(); });
    m_running = true;
    return true;
}

int HttpGateway::port() const
{
    return m_port;
}

void HttpGateway::stop()
{
    if (!m_running) {
        return;
    }

    m_server->stop();
    m_listener.join();
    m_running = false;
}

} // namespace cdrp

