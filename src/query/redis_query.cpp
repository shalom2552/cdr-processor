#include "query/redis_query.hpp"

#include "hiredis/read.h"
#include "store/redis_conn.hpp"
#include "logger.hpp"

#include <hiredis/hiredis.h>
#include <cstddef>
#include <cstdlib>
#include <string>

constexpr std::string_view kComponent = "RedisQuery";

namespace cdrp {

std::string RedisQuery::element_str(const redisReply* element)
{
    return element->type == REDIS_REPLY_STRING ? std::string(element->str, element->len) : std::string();
}

uint64_t RedisQuery::element_score(const redisReply* element)
{
    if (element->type != REDIS_REPLY_STRING) {
        return 0;
    }

    // a score is a double, redis writes it with %.17g
    return static_cast<uint64_t>(std::strtod(std::string(element->str, element->len).c_str(), nullptr));
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

bool RedisQuery::dbsize(uint64_t& out) const
{
    out = 0;

    const char* argv[] = { "DBSIZE" };
    const std::size_t lens[] = { sizeof("DBSIZE") - 1 };

    redisReply* reply = command(1, argv, lens);
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_INTEGER) {
        out = static_cast<uint64_t>(reply->integer);
    }

    freeReplyObject(reply);
    return true;
}

bool RedisQuery::top(std::string_view board, std::size_t offset, std::size_t limit,
                     Ranked& out, uint64_t& count) const
{
    out.clear();
    count = 0;

    const char* cardArgv[] = { "ZCARD", board.data() };
    const std::size_t cardLens[] = { sizeof("ZCARD") - 1, board.size() };

    redisReply* card = command(2, cardArgv, cardLens);
    if (!card) {
        return false;
    }
    if (card->type == REDIS_REPLY_INTEGER) {
        count = static_cast<uint64_t>(card->integer);
    }
    freeReplyObject(card);

    // -1 is the last member, so an unset limit takes the whole board
    const std::string start = std::to_string(offset);
    const std::string stop = limit == 0 ? "-1" : std::to_string(offset + limit - 1);

    const char* argv[] = { "ZREVRANGE", board.data(), start.data(), stop.data(), "WITHSCORES" };
    const std::size_t lens[] = { sizeof("ZREVRANGE") - 1, board.size(), start.size(), stop.size(),
                                 sizeof("WITHSCORES") - 1 };

    redisReply* reply = command(5, argv, lens);
    if (!reply) {
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        out.reserve(reply->elements / 2);
        for (std::size_t i = 0; i + 1 < reply->elements; i += 2) {
            out.emplace_back(element_str(reply->element[i]), element_score(reply->element[i + 1]));
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

