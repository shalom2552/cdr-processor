#include "store/redis_store.hpp"

#include "constants.hpp"
#include "logger.hpp"
#include "store/redis_conn.hpp"

#include <charconv>
#include <cstddef>
#include <string>

constexpr std::string_view kComponent = "RedisStore";

namespace cdrp {

/* Commands this thread queued and has not drained */
static thread_local std::size_t queued = 0;

/* True once this thread queued a MULTI that no EXEC closed yet */
static thread_local bool open_batch = false;

redisContext* RedisStore::batch()
{
    redisContext* broken = RedisConn::peek();
    if (broken && broken->err) {
        // the queued commands went with it
        RedisConn::drop();
        queued = 0;
        open_batch = false;
    }

    redisContext* ctx = RedisConn::get();
    if (!ctx) {
        return nullptr;
    }

    if (!open_batch) {
        if (redisAppendCommand(ctx, "MULTI") != REDIS_OK) {
            logError(kComponent, ctx->errstr);
            return nullptr;
        }
        open_batch = true;
        ++queued;
    }

    return ctx;
}

bool RedisStore::increment(std::string_view key, std::string_view field, uint64_t value)
{
    redisContext* ctx = batch();
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
    // reads the +QUEUED replies, the batch stays open
    return queued < kRedisPipelineDepth || drain();
}

bool RedisStore::mark(std::string_view source, uint64_t seq)
{
    redisContext* ctx = batch();
    if (!ctx) {
        return false;
    }

    const int rc = redisAppendCommand(ctx, "HSET %b %b %llu", kProgressKey.data(), kProgressKey.size(),
                                      source.data(), source.size(), static_cast<unsigned long long>(seq));
    if (rc != REDIS_OK) {
        logError(kComponent, ctx->errstr);
        return false;
    }

    ++queued;
    return true;
}

uint64_t RedisStore::resume_at(std::string_view source)
{
    flush(); // no reply may sit ahead of this one

    redisContext* ctx = RedisConn::get();
    if (!ctx) {
        return 0;
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "HGET %b %b",
        kProgressKey.data(), kProgressKey.size(), source.data(), source.size()));
    if (!reply) {
        logError(kComponent, ctx->errstr);
        RedisConn::drop();
        return 0;
    }

    uint64_t seq = 0;
    if (reply->type == REDIS_REPLY_STRING) {
        std::from_chars(reply->str, reply->str + reply->len, seq);
    } else if (reply->type == REDIS_REPLY_ERROR) {
        logWarn(kComponent, reply->str ? reply->str : "progress read rejected");
    }
    freeReplyObject(reply);

    return seq;
}

bool RedisStore::flush()
{
    if (!open_batch) {
        return drain();
    }

    redisContext* ctx = RedisConn::peek();
    if (!ctx || ctx->err) {
        logError(kComponent, "lost " + std::to_string(queued) + " queued commands");
        queued = 0;
        open_batch = false;
        return false;
    }

    const int rc = redisAppendCommand(ctx, "EXEC");
    open_batch = false;
    if (rc != REDIS_OK) {
        logError(kComponent, ctx->errstr);
        queued = 0;
        return false;
    }

    ++queued;
    return drain();
}

bool RedisStore::drain()
{
    if (queued == 0) {
        return true;
    }

    redisContext* ctx = RedisConn::peek();
    if (!ctx || ctx->err) {
        logError(kComponent, "lost " + std::to_string(queued) + " queued commands");
        queued = 0;
        open_batch = false;
        return false;
    }

    bool ok = true;

    for (std::size_t i = 0; i < queued; ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(ctx, reinterpret_cast<void**>(&reply)) != REDIS_OK) {
            logError(kComponent, ctx->errstr);
            open_batch = false; // the batch went with it
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

