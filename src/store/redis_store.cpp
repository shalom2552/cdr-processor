#include "store/redis_store.hpp"

#include "constants.hpp"
#include "logger.hpp"
#include "store/redis_conn.hpp"

#include <cstddef>
#include <string>

constexpr std::string_view kComponent = "RedisStore";

namespace cdrp {

/* Commands this thread queued and has not drained */
static thread_local std::size_t queued = 0;

bool RedisStore::increment(std::string_view key, std::string_view field, uint64_t value)
{
    redisContext* broken = RedisConn::peek();
    if (broken && broken->err) {
        // the queued commands went with it
        RedisConn::drop();
        queued = 0;
    }

    redisContext* ctx = RedisConn::get();
    if (!ctx) {
        return false;
    }

    const int rc = redisAppendCommand(ctx, "HINCRBY %b %b %llu", key.data(), key.size(),
                                      field.data(), field.size(), static_cast<unsigned long long>(value));
    if (rc != REDIS_OK) {
        logError(kComponent, ctx->errstr);
        return false;
    }

    ++queued;
    return queued < kRedisPipelineDepth || flush();
}

bool RedisStore::flush()
{
    if (queued == 0) {
        return true;
    }

    redisContext* ctx = RedisConn::peek();
    if (!ctx || ctx->err) {
        logError(kComponent, "lost " + std::to_string(queued) + " queued commands");
        queued = 0;
        return false;
    }

    bool ok = true;

    for (std::size_t i = 0; i < queued; ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(ctx, reinterpret_cast<void**>(&reply)) != REDIS_OK) {
            logError(kComponent, ctx->errstr);
            ok = false;
            break; // context is broken
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            logWarn(kComponent, reply->str ? reply->str : "command rejected");
            ok = false;
        }
        freeReplyObject(reply);
    }

    logDebug(kComponent, "drained " + std::to_string(queued) + " commands");
    queued = 0;
    return ok;
}

} // namespace cdrp

