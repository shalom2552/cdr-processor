#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

namespace cdrp {

/**
 * A read side of a key/field counter store.
 * Thread-safe: several threads call it at once.
 *
 * Every call returns false only when the store could not be reached. A key that
 * does not exist is a success with an empty out.
 */
class IQueryStore {
public:
    using Fields = std::vector<std::pair<std::string, std::string>>;

    /* A board's members with their scores */
    using Ranked = std::vector<std::pair<std::string, uint64_t>>;

    virtual ~IQueryStore() = default;

    /**
     * Reads every field of one key.
     *
     * @param key: the key holding the fields
     * @param out: filled with the field/value pairs, cleared first
     * @return false when the read failed
     */
    virtual bool hgetall(const std::string_view key, Fields& out) const = 0;

    /**
     * Reads the field names of one key, without their values.
     *
     * @param key: the key holding the fields
     * @param out: filled with the field names, cleared first
     * @return false when the read failed
     */
    virtual bool hkeys(const std::string_view key, std::vector<std::string>& out) const = 0;

    /**
     * Reads named fields of one key.
     *
     * @param key: the key holding the fields
     * @param field_names: the names to read
     * @param out: one entry per name, empty where the field is missing, cleared first
     * @return false when the read failed
     */
    virtual bool hmget(const std::string_view key, const std::vector<std::string>& field_names,
                       std::vector<std::string>& out) const = 0;

    /**
     * Reads how many keys the store holds.
     *
     * @param out: filled with the key count
     * @return false when the read failed
     */
    virtual bool dbsize(uint64_t& out) const = 0;

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
                     Ranked& out, uint64_t& count) const = 0;
};

} // namespace cdrp

