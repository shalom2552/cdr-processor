#pragma once
#include "parser/iparser.hpp"
#include "source/icdr_source.hpp"
#include "source/rabbit_conn.hpp"
#include "cdr_record.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace cdrp {

/**
 * Reads CDR records from one RabbitMQ connection.
 * Consumes messages, parses every body, hands back records in batches.
 * Keeps the tag of the last message taken, so the caller can ack a whole batch.
 */
class RabbitSource : public ICdrSource {
public:
    /**
     * Constructor, builds the parser named by the config.
     * Throws when no parser is registered under that name.
     *
     * @param connection: the connection the messages are consumed from
     */
    RabbitSource(RabbitConn& connection);

    /**
     * Consumes up to kRabbitBatchSize records, returning early once the queue runs dry.
     *
     * @param out: the vector filled with the parsed records, cleared first
     * @return OK while the connection holds, DONE once stopped, FAIL when the connection is gone
     */
    Status next(std::vector<CdrRecord>& out) override;

    /* The delivery tag of the last message taken off the queue */
    uint64_t last_tag() const;

    /* Ends the batch in progress and makes every later call return DONE */
    void stop();

    /* How many messages became records */
    uint64_t parsed() const;

    /* How many messages the parser rejected */
    uint64_t rejected() const;

private:
    RabbitConn& m_conn;
    std::unique_ptr<IParser> m_parser;

    uint64_t m_last_tag = 0;
    uint64_t m_parsed = 0;
    uint64_t m_rejected = 0;
    std::atomic<bool> m_stop { false };
};

} // namespace cdrp

