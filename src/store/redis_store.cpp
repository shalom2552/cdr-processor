#include "store/redis_store.hpp"

#include "config.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include <string>

namespace cdrp {

RedisStore::Conn::~Conn()
{
    if (ctx) {
        redisFree(ctx);
    }
}

RedisStore::Conn& RedisStore::raw()
{
    // one connection per thread, freed when the thread ends
    static thread_local Conn conn;
    return conn;
}

RedisStore::Conn& RedisStore::conn()
{
    Conn& conn = raw();

    if (conn.ctx && conn.ctx->err == 0) {
        return conn;
    }

    if (conn.ctx) {
        redisFree(conn.ctx);
        conn.ctx = nullptr;
        conn.queued = 0;
    }

    const timeval timeout{cfg.redis.timeout_ms / 1000, (cfg.redis.timeout_ms % 1000) * 1000};
    redisContext* ctx = redisConnectWithTimeout(cfg.redis.host.c_str(), cfg.redis.port, timeout);

    if (!ctx || ctx->err) {
        logError("RedisStore", ctx ? ctx->errstr : "out of memory");
        if (ctx) redisFree(ctx);
        return conn;
    }

    redisSetTimeout(ctx, timeout);
    conn.ctx = ctx;
    logInfo("RedisStore", "connected to " + cfg.redis.host + ":" + std::to_string(cfg.redis.port));
    return conn;
}

bool RedisStore::increment(std::string_view key, std::string_view field, uint64_t value)
{
    Conn& c = conn();
    if (!c.ctx) {
        return false;
    }

    const int rc = redisAppendCommand(c.ctx, "HINCRBY %b %b %llu", key.data(), key.size(),
                                      field.data(), field.size(), static_cast<unsigned long long>(value));
    if (rc != REDIS_OK) {
        logError("RedisStore", c.ctx->errstr);
        return false;
    }

    ++c.queued;
    return c.queued < kRedisPipelineDepth || flush();
}

bool RedisStore::flush()
{
    Conn& c = raw();
    if (c.queued == 0) {
        return true;
    }

    if (!c.ctx || c.ctx->err) {
        logError("RedisStore", "lost " + std::to_string(c.queued) + " queued commands");
        c.queued = 0;
        return false;
    }

    bool ok = true;

    for (std::size_t i = 0; i < c.queued; ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(c.ctx, reinterpret_cast<void**>(&reply)) != REDIS_OK) {
            logError("RedisStore", c.ctx->errstr);
            ok = false;
            break; // context is broken
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            logWarn("RedisStore", reply->str ? reply->str : "command rejected");
            ok = false;
        }
        freeReplyObject(reply);
    }

    logDebug("RedisStore", "drained " + std::to_string(c.queued) + " commands");
    c.queued = 0;
    return ok;
}

} // namespace cdrp

