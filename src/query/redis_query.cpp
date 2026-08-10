#include "query/redis_query.hpp"

#include "hiredis/read.h"
#include "store/redis_conn.hpp"
#include "logger.hpp"

#include <hiredis/hiredis.h>
#include <cstddef>
#include <string>

constexpr std::string_view kComponent = "RedisQuery";

namespace cdrp {

std::string RedisQuery::element_str(const redisReply* element)
{
    return element->type == REDIS_REPLY_STRING ? std::string(element->str, element->len) : std::string();
}

bool RedisQuery::hgetall(const std::string_view key, Fields& out) const
{
    out.clear();

    const char* argv[] = { "HGETALL", key.data() };
    const std::size_t lens[] = { sizeof("HGETALL") - 1, key.size() };

    redisReply* reply = command(2, argv, lens);
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        out.reserve(reply->elements / 2);
        for (std::size_t i = 0; i + 1 < reply->elements; i += 2) {
            out.emplace_back(element_str(reply->element[i]), element_str(reply->element[i + 1]));
        }
    }

    freeReplyObject(reply);
    return true;
}

bool RedisQuery::hkeys(const std::string_view key, std::vector<std::string>& out) const
{
    out.clear();

    const char* argv[] = { "HKEYS", key.data() };
    const std::size_t lens[] = { sizeof("HKEYS") - 1, key.size() };

    redisReply* reply = command(2, argv, lens);
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        out.reserve(reply->elements);
        for (std::size_t i = 0; i < reply->elements; ++i) {
            out.push_back(element_str(reply->element[i]));
        }
    }

    freeReplyObject(reply);
    return true;
}

bool RedisQuery::hmget(const std::string_view key, const std::vector<std::string>& field_names,
                    std::vector<std::string>& out) const
{
    out.clear();
    if (field_names.empty()) {
        return true;
    }

    std::vector<const char*> argv;
    std::vector<std::size_t> lens;
    argv.reserve(field_names.size() + 2);
    lens.reserve(field_names.size() + 2);

    argv.push_back("HMGET");
    lens.push_back(sizeof("HMGET") - 1);
    argv.push_back(key.data());
    lens.push_back(key.size());
    for (const auto& field : field_names) {
        argv.push_back(field.data());
        lens.push_back(field.size());
    }

    redisReply* reply = command(static_cast<int>(argv.size()), argv.data(), lens.data());
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        out.reserve(reply->elements);
        for (std::size_t i = 0; i < reply->elements; ++i) {
            out.push_back(element_str(reply->element[i]));
        }
    }

    freeReplyObject(reply);
    return true;
}

redisReply* RedisQuery::command(int argc, const char** argv, const std::size_t* lens)
{
    redisContext* ctx = RedisConn::get();
    if (!ctx) {
        return nullptr;
    }

    auto* reply = static_cast<redisReply*>(redisCommandArgv(ctx, argc, argv, lens));
    if (!reply) {
        logError(kComponent, ctx->errstr);
        RedisConn::drop();
        return nullptr;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        logWarn(kComponent, reply->str ? reply->str : "command rejected");
        freeReplyObject(reply);
        return nullptr;
    }

    logDebug(kComponent, std::string(argv[0]) + " read " + std::to_string(reply->elements)
                             + " elements");
    return reply;
}

} // namespace cdrp

