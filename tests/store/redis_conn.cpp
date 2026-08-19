#include "doctest.h"
#include "config.hpp"
#include "store/redis_conn.hpp"
#include "store/redis_store.hpp"

#include <hiredis/hiredis.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

using namespace cdrp;

/* A key of this suite's own, so a live server keeps none of it that matters */
const std::string kKey = "cdrp:test:conn";

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
        freeReplyObject(redisCommand(ctx, "DEL %b", kKey.data(), kKey.size()));
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

/* A port nothing listens on, taken by binding one and letting it go */
int closedPort()
{
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 1;
    }
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    socklen_t length = sizeof(address);
    if (bind(sock, reinterpret_cast<sockaddr*>(&address), length) != 0
        || getsockname(sock, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        close(sock);
        return 1;
    }
    close(sock);
    return ntohs(address.sin_port);
}

/* Points cfg.redis at another port for one test, and puts the old one back */
struct PortSwap {
    int saved = cfg.redis.port;

    explicit PortSwap(int port)
    {
        const_cast<Config&>(cfg).redis.port = port;
    }

    ~PortSwap()
    {
        const_cast<Config&>(cfg).redis.port = saved;
    }
};

/* True when the context answers a PING */
bool answers(redisContext* ctx)
{
    if (!ctx) {
        return false;
    }
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply) freeReplyObject(reply);
    return ok;
}

} // namespace

using namespace cdrp;

TEST_CASE("redis_conn_peeks_at_nothing_before_the_first_get")
{
    bool empty = false;

    std::thread thread([&] { empty = RedisConn::peek() == nullptr; });
    thread.join();

    CHECK(empty);
}

TEST_CASE("redis_conn_hands_one_thread_the_same_context_twice")
{
    if (!serverUp()) {
        return;
    }
    redisContext* first = nullptr;
    redisContext* second = nullptr;

    std::thread thread([&] {
        first = RedisConn::get();
        second = RedisConn::get();
    });
    thread.join();

    CHECK(first != nullptr);
    CHECK(first == second);
}

TEST_CASE("redis_conn_peeks_at_the_context_the_thread_opened")
{
    if (!serverUp()) {
        return;
    }
    redisContext* opened = nullptr;
    redisContext* peeked = nullptr;

    std::thread thread([&] {
        opened = RedisConn::get();
        peeked = RedisConn::peek();
    });
    thread.join();

    CHECK(opened != nullptr);
    CHECK(opened == peeked);
}

TEST_CASE("redis_conn_hands_two_threads_two_contexts")
{
    if (!serverUp()) {
        return;
    }
    redisContext* first = nullptr;
    redisContext* second = nullptr;
    std::atomic<int> holding { 0 };

    // both threads hold their context at once, so a freed address cannot be reused
    auto take = [&](redisContext*& out) {
        out = RedisConn::get();
        ++holding;
        while (holding.load() < 2) {
            std::this_thread::yield();
        }
    };

    std::thread one([&] { take(first); });
    std::thread two([&] { take(second); });
    one.join();
    two.join();

    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(first != second);
}

TEST_CASE("redis_conn_opens_a_working_context_after_a_drop")
{
    if (!serverUp()) {
        return;
    }
    bool dropped = false;
    bool alive = false;

    std::thread thread([&] {
        RedisConn::get();
        RedisConn::drop();
        dropped = RedisConn::peek() == nullptr;
        alive = answers(RedisConn::get());
    });
    thread.join();

    CHECK(dropped);
    CHECK(alive);
}

TEST_CASE("redis_conn_backs_off_instead_of_dialling_a_closed_port")
{
    const PortSwap swap(closedPort());
    bool first = false;
    bool second = false;
    long long firstMs = 0;
    long long secondMs = 0;
    long long restMs = 0;

    std::thread thread([&] {
        firstMs = millisOf([&] { first = RedisConn::get() == nullptr; });
        secondMs = millisOf([&] { second = RedisConn::get() == nullptr; });
        restMs = millisOf([&] {
            for (int index = 0; index < 20; ++index) {
                RedisConn::get();
            }
        });
    });
    thread.join();

    CHECK(first);
    CHECK(second);
    CHECK(firstMs < cfg.redis.timeout_ms * 2);
    CHECK(secondMs < cfg.redis.timeout_ms / 4);
    CHECK(restMs < cfg.redis.timeout_ms / 2);
}
