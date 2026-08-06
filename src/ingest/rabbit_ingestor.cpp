#include "ingest/rabbit_ingestor.hpp"

#include "config.hpp"
#include "logger.hpp"
#include "parser/parser_factory.hpp"
#include "source/icdr_source.hpp"
#include "source/rabbit_conn.hpp"
#include "source/rabbit_source.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cdrp {

RabbitIngestor::RabbitIngestor(ISink& sink)
    : m_format(cfg.source.format)
    , m_sink(sink)
{
}

RabbitIngestor::~RabbitIngestor()
{
    stop();
}

bool RabbitIngestor::start()
{
    if (m_running) {
        return true;
    }

    if (!ParserFactory::instance().hasParser(m_format)) {
        logError("RabbitIngestor", "Unknown format: " + m_format);
        return false;
    }

    m_conns.reserve(cfg.rabbit.consumers);
    for (std::size_t i = 0; i < cfg.rabbit.consumers; ++i) {
        auto connection = std::make_unique<RabbitConn>();
        if (!connection->open(cfg.rabbit.url, cfg.rabbit.queue)) {
            logWarn("RabbitIngestor", "consumer-" + std::to_string(i) + ": connect failed");
            continue;
        }
        m_conns.push_back(std::move(connection));
    }

    if (m_conns.empty()) {
        logError("RabbitIngestor", "no consumer could connect to " + cfg.rabbit.url);
        return false;
    }

    m_stop.store(false, std::memory_order_relaxed);
    m_threads.reserve(m_conns.size());
    for (std::size_t i = 0; i < m_conns.size(); ++i) {
        m_threads.emplace_back(&RabbitIngestor::consume, this, i);
    }

    m_running = true;
    logInfo("RabbitIngestor", "started with " + std::to_string(m_conns.size()) + " consumers");
    return true;
}

void RabbitIngestor::stop()
{
    if (!m_running) {
        return;
    }
    m_stop.store(true, std::memory_order_relaxed);

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_threads.clear();
    m_conns.clear();

    m_running = false;
    logInfo("RabbitIngestor", "stopped");
}

void RabbitIngestor::consume(std::size_t id)
{
    const std::string tag = "consumer-" + std::to_string(id);

    RabbitConn& connection = *m_conns[id];
    RabbitSource source(connection);
    std::vector<CdrRecord> batch;
    uint64_t total = 0;

    while (!m_stop.load(std::memory_order_relaxed)) {
        const auto status = source.next(batch);

        if (status == RabbitSource::Status::FAIL) {
            logWarn("RabbitIngestor", tag + ": read failed, exiting");
            break;
        }
        if (batch.empty()) {
            continue;
        }

        logDebug("RabbitIngestor", tag + ": batch of " + std::to_string(batch.size()));
        m_sink.consume(batch);
        total += batch.size();

        if (!connection.ack(source.last_tag(), true)) {
            logWarn("RabbitIngestor", tag + ": ack failed, exiting");
            break;
        }
    }

    logInfo("RabbitIngestor", tag + ": " + std::to_string(total) + " records, " + std::to_string(source.rejected()) + " rejected");
}

} // namespace cdrp

