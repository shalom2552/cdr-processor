#pragma once

#include <cstdint>
#include <string_view>

namespace cdrp {

/**
 * A key/field counter store.
 * Thread-safe: several threads call increment() and flush() at once.
 */
class IStore {
public:
    virtual ~IStore() = default;

    /**
     * Adds value to one field of one key, creating either if missing.
     *
     * @param key: the key holding the fields
     * @param field: the counter within the key
     * @param value: the amount to add
     * @return false when the write could not be queued
     */
    virtual bool increment(std::string_view key, std::string_view field, uint64_t value) = 0;

    /* Completes every increment queued so far. False when any of them failed. */
    virtual bool flush() = 0;
};

} // namespace cdrp

