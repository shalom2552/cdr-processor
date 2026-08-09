#pragma once

#include "store/istore.hpp"

namespace cdrp {

/**
 * Keeps counters in Redis, one hash per key, every increment an HINCRBY.
 * Order does not matter, so batches may be written from several threads at once.
 * Each thread opens its own connection and pipeline on first use.
 */
class RedisStore : public IStore {
public:
    /**
     * Queues one HINCRBY, draining the pipeline once it holds kRedisPipelineDepth.
     *
     * @return false when the connection is gone or the command was refused
     */
    bool increment(std::string_view key, std::string_view field, uint64_t value) override;

    /* Reads the replies of every queued command, leaving the pipeline empty */
    bool flush() override;

};

} // namespace cdrp

