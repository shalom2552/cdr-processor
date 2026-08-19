#include "ingest/rabbit_ingestor.hpp"

#include "config.hpp"
#include "logger.hpp"
#include "parser/parser_factory.hpp"
#include "source/icdr_source.hpp"
#include "source/rabbit_conn.hpp"
#include "source/rabbit_source.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

constexpr std::string_view kComponent = "RabbitIngestor";

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
        logError(kComponent, "Unknown format: " + m_format);
        return false;
    }

    m_conns.reserve(cfg.rabbit.consumers);
    for (std::size_t i = 0; i < cfg.rabbit.consumers; ++i) {
        m_conns.push_back(std::make_unique<RabbitConn>());
    }

    m_stop.store(false, std::memory_order_relaxed);
    m_threads.reserve(m_conns.size());
    for (std::size_t i = 0; i < m_conns.size(); ++i) {
        m_threads.emplace_back(&RabbitIngestor::consume, this, i);
    }

    m_running = true;
    logInfo(kComponent, "started with " + std::to_string(m_conns.size()) + " consumers");
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
    logInfo(kComponent, "stopped");
}

void RabbitIngestor::consume(std::size_t id)
{
    const std::string tag = "consumer " + std::to_string(id);

    RabbitConn& connection = *m_conns[id];
    std::vector<CdrRecord> batch;
    uint64_t total = 0;
    uint64_t rejected = 0;
    unsigned delay_ms = 0;
    bool down = false;

    while (!m_stop.load(std::memory_order_relaxed)) {
        if (!connection.open(cfg.rabbit.url, cfg.rabbit.queue)) {
            delay_ms = delay_ms ? std::min(delay_ms * 2, kRabbitBackoffMaxMs) : kRabbitBackoffMinMs;
            if (!down && id == 0) {
                logWarn(kComponent, tag + ": connect to " + cfg.rabbit.url + " failed, retrying");
            } else {
                logDebug(kComponent, tag + ": connect failed, retry in " + std::to_string(delay_ms) + "ms");
            }
            down = true;
            backoff(delay_ms);
            continue;
        }

        if (down) {
            logInfo(kComponent, tag + ": reconnected");
        }
        delay_ms = 0;
        down = false;

        RabbitSource source(connection); // a fresh channel restarts the delivery tags

        while (!m_stop.load(std::memory_order_relaxed)) {
            const auto status = source.next(batch);

            if (status == RabbitSource::Status::FAIL) {
                logWarn(kComponent, tag + ": read failed, reconnecting");
                break;
            }
            if (batch.empty()) {
                continue;
            }

            logDebug(kComponent, tag + ": batch of " + std::to_string(batch.size()));
            m_sink.consume(batch, ""); // the queue keeps no progress
            total += batch.size();

            if (!connection.ack(source.last_tag(), true)) {
                logWarn(kComponent, tag + ": ack failed, reconnecting");
                break;
            }
        }

        rejected += source.rejected();
    }

    logInfo(kComponent, tag + ": " + std::to_string(total) + " records, " + std::to_string(rejected) + " rejected");
}

void RabbitIngestor::backoff(unsigned delay_ms)
{
    unsigned left = delay_ms;
    while (left > 0 && !m_stop.load(std::memory_order_relaxed)) {
        const unsigned slice = std::min(left, kRabbitBackoffSliceMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        left -= slice;
    }
}

} // namespace cdrp

