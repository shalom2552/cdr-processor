#pragma once

#include "store/istore.hpp"

#include <hiredis/hiredis.h>

namespace cdrp {

/**
 * Keeps counters in Redis, one hash per key, every increment an HINCRBY.
 * Order does not matter, so batches may be written from several threads at once.
 * Each thread opens its own connection and pipeline on first use, and wraps whatever
 * it queues between two flushes in one transaction.
 */
class RedisStore : public IStore {
public:
    /**
     * Queues one HINCRBY, draining the pipeline once it holds kRedisPipelineDepth.
     *
     * @return false when the connection is gone or the command was refused
     */
    bool increment(std::string_view key, std::string_view field, uint64_t value) override;

    /**
     * Queues one ZINCRBY into the same batch, so a board commits with its counters.
     *
     * @return false when the connection is gone or the command was refused
     */
    bool rank(std::string_view board, std::string_view member, uint64_t value) override;

    /* Closes the batch with an EXEC and reads the replies, leaving the pipeline empty */
    bool flush() override;

    /* One HGET of the source's field of the progress hash, 0 when it holds none */
    uint64_t resume_at(std::string_view source) override;

    /* Queues one HSET into the open batch, so the mark commits with the counters */
    bool mark(std::string_view source, uint64_t seq) override;

private:
    /* This thread's context with a batch open on it, null when there is no connection */
    redisContext* batch();

    /* Reads one reply per queued command, leaving the batch as it stands */
    bool drain();
};

} // namespace cdrp

