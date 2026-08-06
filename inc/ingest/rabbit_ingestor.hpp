#pragma once

#include "ingest/iingestor.hpp"
#include "sink/isink.hpp"
#include "source/rabbit_conn.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace cdrp {

/**
 * Turns the messages of a RabbitMQ queue into records.
 * Each consumer thread holds its own connection, parses what it takes and feeds the sink.
 */
class RabbitIngestor : public IIngestor {
public:
    /**
     * Constructor, takes the format to parse from the config.
     *
     * @param sink: the sink every parsed batch is handed to
     */
    RabbitIngestor(ISink& sink);
    ~RabbitIngestor() override;

    /**
     * Opens one connection per configured consumer and starts a thread on each.
     * False if the format has no parser or no connection could be opened; the
     * ingestor stays stopped.
     *
     * @return true once the consumers are running
     */
    bool start() override;

    /* Stops the consumers, closes their connections and returns once they are joined */
    void stop() override;

private:
    /* Consume, parse and ack batches on connection id until stopped */
    void consume(std::size_t id);

private:
    std::string m_format;
    ISink& m_sink;

    std::vector<std::unique_ptr<RabbitConn>> m_conns;
    std::vector<std::thread> m_threads;
    std::atomic<bool> m_stop = false;
    bool m_running = false;
};

} // namespace cdrp

