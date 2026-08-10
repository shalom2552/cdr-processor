#pragma once

#include "query/iquery_store.hpp"
#include "hiredis/hiredis.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace cdrp {

/**
 * The read side of the Redis counter store.
 * Reads the fields of one key over this thread's shared connection.
 */
class RedisQuery : public IQueryStore {
public:

    /**
     * Reads every field of one key.
     *
     * @param key: the key holding the fields
     * @param out: filled with the field/value pairs, cleared first
     * @return false when the read failed
     */
    virtual bool hgetall(const std::string_view key, Fields& out) const override;

    /**
     * Reads the field names of one key, without their values.
     *
     * @param key: the key holding the fields
     * @param out: filled with the field names, cleared first
     * @return false when the read failed
     */
    virtual bool hkeys(const std::string_view key, std::vector<std::string>& out) const override;

    /**
     * Reads named fields of one key.
     *
     * @param key: the key holding the fields
     * @param field_names: the names to read
     * @param out: one entry per name, empty where the field is missing, cleared first
     * @return false when the read failed
     */
    virtual bool hmget(const std::string_view key, const std::vector<std::string>& field_names,
                       std::vector<std::string>& out) const override;

    /**
     * Reads how many keys the store holds.
     *
     * @param out: filled with the key count
     * @return false when the read failed
     */
    virtual bool dbsize(uint64_t& out) const override;

    /**
     * Reads one page of a board, highest score first.
     *
     * @param board: the key holding the members
     * @param offset: the members skipped
     * @param limit: the members returned, 0 for every one of them
     * @param out: filled with the member/score pairs, cleared first
     * @param count: filled with the members the board holds, whether returned or not
     * @return false when the read failed
     */
    virtual bool top(std::string_view board, std::size_t offset, std::size_t limit,
                     Ranked& out, uint64_t& count) const override;

private:
    /* Runs one command on this thread's connection, null when it failed or was rejected */
    static redisReply* command(int argc, const char** argv, const std::size_t* lens);

    /* One reply element as a string, empty when it is not a string */
    static std::string element_str(const redisReply* element);

    /* One reply element as a score, 0 when it is not a number */
    static uint64_t element_score(const redisReply* element);
};

} // namespace cdrp

