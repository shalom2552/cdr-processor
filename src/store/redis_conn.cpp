#include "store/redis_conn.hpp"

#include "config.hpp"
#include "logger.hpp"

#include <string>

constexpr std::string_view kComponent = "RedisStore";

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

    const timeval timeout{cfg.redis.timeout_ms / 1000, (cfg.redis.timeout_ms % 1000) * 1000};
    redisContext* ctx = redisConnectWithTimeout(cfg.redis.host.c_str(), cfg.redis.port, timeout);

    if (!ctx || ctx->err) {
        logError(kComponent, ctx ? ctx->errstr : "out of memory");
        if (ctx) redisFree(ctx);
        return nullptr;
    }

    redisSetTimeout(ctx, timeout);
    held.ctx = ctx;
    logInfo(kComponent, "connected to " + cfg.redis.host + ":" + std::to_string(cfg.redis.port));
    return ctx;
}

} // namespace cdrp

