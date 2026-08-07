#include "doctest.h"
#include "config.hpp"
#include "constants.hpp"
#include "store/redis_store.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace cdrp;

/* A key of this suite's own, so a live server keeps none of it that matters */
const std::string kKey = "cdrp:test:store";

/* The empty key one test writes to */
const std::string kEmptyKey;

/* The oversized key one test writes to */
const std::string kLongKey(4096, 'k');

/* Drops every key this suite wrote, so a live server is left as it was found */
struct Cleanup {
    ~Cleanup()
    {
        const timeval timeout { 1, 0 };
        redisContext* ctx
            = redisConnectWithTimeout(cfg.redis.host.c_str(), cfg.redis.port, timeout);
        if (!ctx || ctx->err) {
            if (ctx) redisFree(ctx);
            return;
        }
        for (const std::string* key : { &kKey, &kEmptyKey, &kLongKey }) {
            freeReplyObject(redisCommand(ctx, "DEL %b", key->data(), key->size()));
        }
        redisFree(ctx);
    }
};

const Cleanup cleanup;

/* True when the configured server answered one write, asked once for the whole suite */
bool serverUp()
{
    static const bool up = [] {
        RedisStore store;
        return store.increment(kKey, "probe", 1) && store.flush();
    }();
    return up;
}

/* Milliseconds a call took, so a test can bound a wait */
template <typename Fn>
long long millisOf(Fn fn)
{
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

} // namespace

using namespace cdrp;

TEST_CASE("redis_store_is_a_store")
{
    CHECK(std::is_base_of<IStore, RedisStore>::value);
    CHECK(std::has_virtual_destructor<IStore>::value);
}

TEST_CASE("redis_store_flushes_when_nothing_was_queued")
{
    RedisStore store;

    CHECK(store.flush());
}

TEST_CASE("redis_store_flushes_twice_in_a_row")
{
    RedisStore store;

    CHECK(store.flush());
    CHECK(store.flush());
}

TEST_CASE("redis_store_answers_an_increment_the_same_way_the_server_is_reachable")
{
    RedisStore store;
    bool queued = false;

    const long long elapsed = millisOf([&] { queued = store.increment(kKey, "one", 1); });

    CHECK(queued == serverUp());
    CHECK(elapsed < 5000);
    CHECK(store.flush());
}

TEST_CASE("redis_store_takes_an_increment_of_zero")
{
    RedisStore store;

    CHECK(store.increment(kKey, "zero", 0) == serverUp());
    CHECK(store.flush());
}

TEST_CASE("redis_store_takes_a_counter_larger_than_a_32_bit_total")
{
    RedisStore store;

    CHECK(store.increment(kKey, "wide", 8589934592ULL) == serverUp());
    CHECK(store.flush());
}

TEST_CASE("redis_store_takes_an_empty_key_and_field")
{
    RedisStore store;

    CHECK(store.increment(kEmptyKey, "", 1) == serverUp());
    CHECK(store.flush());
}

TEST_CASE("redis_store_takes_a_key_and_field_that_are_not_terminated")
{
    RedisStore store;
    const std::string buffer = kKey + "xxxx";
    const std::string_view key(buffer.data(), kKey.size());

    CHECK(store.increment(key, std::string_view("longfield", 4), 1) == serverUp());
    CHECK(store.flush());
}

TEST_CASE("redis_store_takes_a_long_key")
{
    RedisStore store;

    CHECK(store.increment(kLongKey, "long", 1) == serverUp());
    CHECK(store.flush());
}

TEST_CASE("redis_store_shares_one_instance_between_calls")
{
    RedisStore store;

    for (int index = 0; index < 8; ++index) {
        CHECK(store.increment(kKey, "shared", 1) == serverUp());
    }

    CHECK(store.flush());
}

TEST_CASE("redis_store_drains_a_pipeline_that_fills_up")
{
    if (!serverUp()) {
        return;
    }
    RedisStore store;
    bool queued = true;

    const long long elapsed = millisOf([&] {
        for (std::size_t index = 0; index < kRedisPipelineDepth + 16; ++index) {
            queued = store.increment(kKey, "deep", 1) && queued;
        }
    });

    CHECK(queued);
    CHECK(store.flush());
    CHECK(elapsed < 30000);
}

TEST_CASE("redis_store_writes_from_several_threads_at_once")
{
    RedisStore store;
    const bool up = serverUp();
    std::vector<std::thread> threads;
    std::vector<char> ok(4, 0);

    const long long elapsed = millisOf([&] {
        for (std::size_t index = 0; index < ok.size(); ++index) {
            threads.emplace_back([&, index] {
                bool queued = true;
                for (int round = 0; round < 16; ++round) {
                    queued = store.increment(kKey, "threaded", 1) && queued;
                }
                ok[index] = static_cast<char>(queued && store.flush());
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    });

    for (const char result : ok) {
        CHECK(static_cast<bool>(result) == up);
    }
    CHECK(elapsed < 30000);
}

TEST_CASE("redis_store_keeps_answering_after_a_failed_write")
{
    RedisStore store;

    CHECK(store.increment(kKey, "after", 1) == serverUp());
    CHECK(store.flush());
    CHECK(store.increment(kKey, "after", 1) == serverUp());
    CHECK(store.flush());
}
