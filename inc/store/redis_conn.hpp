#pragma once

#include <hiredis/hiredis.h>

#include <chrono>

namespace cdrp {

/**
 * Holds one Redis connection per thread, opened on first use from cfg.redis.
 * A context belongs to the thread that opened it, and is freed when that thread ends.
 * A failed connect backs the thread off, so a dead server is cheap to ask again.
 */
class RedisConn {
public:
    /* This thread's context, opened on first use, reopened when it broke, null while backing off */
    static redisContext* get();

    /* This thread's context as it stands, without opening one */
    static redisContext* peek();

    /* Drops this thread's context, so the next get() opens a new one */
    static void drop();

private:
    /* This thread's context, freed when the thread ends */
    struct Holder {
        redisContext* ctx = nullptr;
        std::chrono::steady_clock::time_point next_try{};
        unsigned delay_ms = 0;
        bool logged_down = false;
        ~Holder();
    };

    static Holder& holder();

};

} // namespace cdrp

