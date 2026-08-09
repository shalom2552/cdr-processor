#include "doctest.h"
#include "config.hpp"
#include "store/redis_query.hpp"
#include "store/redis_store.hpp"

#include <hiredis/hiredis.h>

#include <chrono>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace cdrp;

/* A key of this suite's own, so a live server keeps none of it that matters */
const std::string kKey = "cdrp:test:query";

/* The key no test ever writes to */
const std::string kMissingKey = "cdrp:test:query:missing";

/* The empty key one test reads */
const std::string kEmptyKey;

/* The oversized key one test reads */
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
        for (const std::string* key : { &kKey, &kMissingKey, &kEmptyKey, &kLongKey }) {
            freeReplyObject(redisCommand(ctx, "DEL %b", key->data(), key->size()));
        }
        redisFree(ctx);
    }
};

const Cleanup cleanup;

/* True when the configured server answered one read, asked once for the whole suite */
bool serverUp()
{
    static const bool up = [] {
        RedisQuery query;
        std::vector<std::string> names;
        return query.hkeys(kMissingKey, names);
    }();
    return up;
}

/* Writes the fields one test reads back, true when the server took them */
bool seed()
{
    static const bool written = [] {
        RedisStore store;
        const bool queued = store.increment(kKey, "voice_out", 60)
            && store.increment(kKey, "voice_in", 40);
        return queued && store.flush();
    }();
    return written;
}

/* The value of one field, or an empty string when the pairs hold no such field */
std::string valueOf(const IQueryStore::Fields& fields, const std::string& name)
{
    for (const auto& field : fields) {
        if (field.first == name) {
            return field.second;
        }
    }
    return std::string();
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

TEST_CASE("redis_query_is_a_query_store")
{
    CHECK(std::is_base_of<IQueryStore, RedisQuery>::value);
    CHECK(std::has_virtual_destructor<IQueryStore>::value);
}

TEST_CASE("redis_query_hgetall_answers_the_same_way_the_server_is_reachable")
{
    RedisQuery query;
    IQueryStore::Fields fields;
    bool read = false;

    const long long elapsed = millisOf([&] { read = query.hgetall(kMissingKey, fields); });

    CHECK(read == serverUp());
    CHECK(elapsed < 5000);
}

TEST_CASE("redis_query_hgetall_reads_no_fields_for_a_missing_key")
{
    RedisQuery query;
    IQueryStore::Fields fields;

    CHECK(query.hgetall(kMissingKey, fields) == serverUp());

    CHECK(fields.empty());
}

TEST_CASE("redis_query_hgetall_clears_out_before_it_reads")
{
    RedisQuery query;
    IQueryStore::Fields fields { { "stale", "value" } };

    CHECK(query.hgetall(kMissingKey, fields) == serverUp());

    CHECK(fields.empty());
}

TEST_CASE("redis_query_hkeys_reads_no_names_for_a_missing_key")
{
    RedisQuery query;
    std::vector<std::string> names;

    CHECK(query.hkeys(kMissingKey, names) == serverUp());

    CHECK(names.empty());
}

TEST_CASE("redis_query_hkeys_clears_out_before_it_reads")
{
    RedisQuery query;
    std::vector<std::string> names { "stale" };

    CHECK(query.hkeys(kMissingKey, names) == serverUp());

    CHECK(names.empty());
}

TEST_CASE("redis_query_hmget_takes_an_empty_field_list")
{
    RedisQuery query;
    std::vector<std::string> values { "stale" };

    CHECK(query.hmget(kKey, std::vector<std::string> {}, values));

    CHECK(values.empty());
}

TEST_CASE("redis_query_hmget_reads_one_empty_entry_per_missing_field")
{
    if (!serverUp()) {
        return;
    }
    RedisQuery query;
    std::vector<std::string> values;

    CHECK(query.hmget(kMissingKey, { "voice_out", "voice_in" }, values));

    CHECK(values.size() == 2);
    CHECK(values[0].empty());
    CHECK(values[1].empty());
}

TEST_CASE("redis_query_hmget_clears_out_before_it_reads")
{
    RedisQuery query;
    std::vector<std::string> values { "stale" };

    query.hmget(kMissingKey, { "voice_out" }, values);

    CHECK((values.empty() || values[0].empty()));
}

TEST_CASE("redis_query_reads_back_the_fields_the_store_wrote")
{
    if (!serverUp() || !seed()) {
        return;
    }
    RedisQuery query;
    IQueryStore::Fields fields;

    CHECK(query.hgetall(kKey, fields));

    CHECK(fields.size() == 2);
    CHECK(valueOf(fields, "voice_out") == "60");
    CHECK(valueOf(fields, "voice_in") == "40");
}

TEST_CASE("redis_query_reads_back_the_field_names_the_store_wrote")
{
    if (!serverUp() || !seed()) {
        return;
    }
    RedisQuery query;
    std::vector<std::string> names;

    CHECK(query.hkeys(kKey, names));

    CHECK(names.size() == 2);
}

TEST_CASE("redis_query_hmget_reads_the_named_fields_only")
{
    if (!serverUp() || !seed()) {
        return;
    }
    RedisQuery query;
    std::vector<std::string> values;

    CHECK(query.hmget(kKey, { "voice_in", "missing" }, values));

    CHECK(values.size() == 2);
    CHECK(values[0] == "40");
    CHECK(values[1].empty());
}

TEST_CASE("redis_query_takes_a_key_that_is_not_terminated")
{
    RedisQuery query;
    const std::string buffer = kMissingKey + "xxxx";
    const std::string_view key(buffer.data(), kMissingKey.size());
    IQueryStore::Fields fields;
    std::vector<std::string> names;
    std::vector<std::string> values;

    CHECK(query.hgetall(key, fields) == serverUp());
    CHECK(query.hkeys(key, names) == serverUp());
    CHECK(query.hmget(key, { "voice_out" }, values) == serverUp());

    CHECK(fields.empty());
    CHECK(names.empty());
}

TEST_CASE("redis_query_takes_an_empty_key")
{
    RedisQuery query;
    IQueryStore::Fields fields;
    std::vector<std::string> names;

    CHECK(query.hgetall(kEmptyKey, fields) == serverUp());
    CHECK(query.hkeys(kEmptyKey, names) == serverUp());
}

TEST_CASE("redis_query_takes_a_long_key")
{
    RedisQuery query;
    IQueryStore::Fields fields;
    std::vector<std::string> names;

    CHECK(query.hgetall(kLongKey, fields) == serverUp());
    CHECK(query.hkeys(kLongKey, names) == serverUp());
}

TEST_CASE("redis_query_keeps_answering_after_a_failed_read")
{
    RedisQuery query;
    IQueryStore::Fields fields;

    CHECK(query.hgetall(kMissingKey, fields) == serverUp());
    CHECK(query.hgetall(kMissingKey, fields) == serverUp());
}

TEST_CASE("redis_query_reads_through_the_interface")
{
    RedisQuery query;
    const IQueryStore& store = query;
    IQueryStore::Fields fields;

    CHECK(store.hgetall(kMissingKey, fields) == serverUp());

    CHECK(fields.empty());
}

TEST_CASE("redis_query_reads_from_several_threads_at_once")
{
    RedisQuery query;
    const bool up = serverUp();
    std::vector<std::thread> threads;
    std::vector<char> ok(4, 0);

    const long long elapsed = millisOf([&] {
        for (std::size_t index = 0; index < ok.size(); ++index) {
            threads.emplace_back([&, index] {
                bool read = true;
                for (int round = 0; round < 16; ++round) {
                    IQueryStore::Fields fields;
                    std::vector<std::string> names;
                    read = query.hgetall(kKey, fields) && query.hkeys(kKey, names) && read;
                }
                ok[index] = static_cast<char>(read);
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
