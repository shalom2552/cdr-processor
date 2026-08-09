#include "query/http_gateway.hpp"

#include "query/json.hpp"
#include "config.hpp"
#include "logger.hpp"

#include "httplib.h"
#include <string>
#include <utility>

static constexpr std::string_view kComponent = "HttpGateway";

namespace cdrp {

/* Send one result as the response */
static void send(httplib::Response& res, QueryService::Result result)
{
    res.status = result.status;
    res.set_content(std::move(result.body), "application/json");
}

HttpGateway::HttpGateway(const QueryService& service)
    : m_service(service)
    , m_server(std::make_unique<httplib::Server>())
{
    m_server->new_task_queue = [] {
        return new httplib::ThreadPool(cfg.query.concurrency);
    };

    m_server->Get(R"(/query/msisdn/(\d+))", [this](const httplib::Request& req,
                                                   httplib::Response& res) {
        send(res, m_service.msisdn(req.matches[1].str()));
    });

    m_server->Get(R"(/query/operator/(\d+))", [this](const httplib::Request& req,
                                                     httplib::Response& res) {
        send(res, m_service.op(req.matches[1].str()));
    });

    m_server->Get(R"(/query/link/(\d+))", [this](const httplib::Request& req,
                                                 httplib::Response& res) {
        send(res, m_service.peers(req.matches[1].str()));
    });

    m_server->Get(R"(/query/link/(\d+)/(\d+))", [this](const httplib::Request& req,
                                                       httplib::Response& res) {
        send(res, m_service.link(req.matches[1].str(), req.matches[2].str()));
    });

    m_server->Get(R"(/query/path/(\d+)/(\d+))", [this](const httplib::Request& req,
                                                       httplib::Response& res) {
        send(res, m_service.path(req.matches[1].str(), req.matches[2].str()));
    });

    m_server->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            res.set_content(Json::error("no such route"), "application/json");
        }
    });

    m_server->set_exception_handler([](const httplib::Request& req,
                                       httplib::Response& res, std::exception_ptr) {
        logWarn(kComponent, "handler threw on " + req.path);
        res.status = 500;
        res.set_content(Json::error("internal error"), "application/json");
    });
}

HttpGateway::~HttpGateway() = default;

bool HttpGateway::run()
{
    logInfo(kComponent, "starting on port " + std::to_string(cfg.query.port));
    if (!m_server->listen("0.0.0.0", cfg.query.port)) {
        logError(kComponent, "could not bind port " + std::to_string(cfg.query.port));
        return false;
    }
    return true;
}

void HttpGateway::stop()
{
    m_server->stop();
}

} // namespace cdrp

