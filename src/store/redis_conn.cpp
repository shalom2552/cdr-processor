#include "store/redis_conn.hpp"

#include "config.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include <algorithm>
#include <chrono>
#include <string>

constexpr std::string_view kComponent = "RedisConn";

namespace cdrp {

RedisConn::Holder::~Holder()
{
    if (ctx) {
        redisFree(ctx);
    }
}

RedisConn::Holder& RedisConn::holder()
{
    // one connection per thread, freed when the thread ends
    static thread_local Holder holder;
    return holder;
}

redisContext* RedisConn::peek()
{
    return holder().ctx;
}

void RedisConn::drop()
{
    Holder& held = holder();

    if (held.ctx) {
        redisFree(held.ctx);
        held.ctx = nullptr;
    }
}

redisContext* RedisConn::get()
{
    Holder& held = holder();

    if (held.ctx && held.ctx->err == 0) {
        return held.ctx;
    }

    drop();

    if (std::chrono::steady_clock::now() < held.next_try) {
        return nullptr;
    }

    const timeval timeout{cfg.redis.timeout_ms / 1000, (cfg.redis.timeout_ms % 1000) * 1000};
    redisContext* ctx = redisConnectWithTimeout(cfg.redis.host.c_str(), cfg.redis.port, timeout);

    if (!ctx || ctx->err) {
        const std::string reason = ctx ? ctx->errstr : "out of memory";
        if (ctx) redisFree(ctx);

        // first failure of a run speaks up, the rest only at debug
        held.delay_ms = held.delay_ms ? std::min(held.delay_ms * 2, kRedisBackoffMaxMs)
                                      : kRedisBackoffMinMs;
        held.next_try = std::chrono::steady_clock::now() + std::chrono::milliseconds(held.delay_ms);

        if (held.logged_down) {
            logDebug(kComponent, reason + ", next try in " + std::to_string(held.delay_ms) + "ms");
        } else {
            held.logged_down = true;
            logError(kComponent, reason);
        }
        return nullptr;
    }

    redisSetTimeout(ctx, timeout);
    held.ctx = ctx;
    held.delay_ms = 0;
    held.next_try = {};
    held.logged_down = false;
    logInfo(kComponent, "connected to " + cfg.redis.host + ":" + std::to_string(cfg.redis.port));
    return ctx;
}

} // namespace cdrp

