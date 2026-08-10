#pragma once

#include <hiredis/hiredis.h>

namespace cdrp {

/**
 * Holds one Redis connection per thread, opened on first use from cfg.redis.
 * A context belongs to the thread that opened it, and is freed when that thread ends.
 */
class RedisConn {
public:
    /* This thread's context, opened on first use, reopened when it broke, null when it cannot open */
    static redisContext* get();

    /* This thread's context as it stands, without opening one */
    static redisContext* peek();

    /* Drops this thread's context, so the next get() opens a new one */
    static void drop();

private:
    /* This thread's context, freed when the thread ends */
    struct Holder {
        redisContext* ctx = nullptr;
        ~Holder();
    };

    static Holder& holder();

};

} // namespace cdrp

