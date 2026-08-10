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

    /**
     * How far this source was already applied, so the reader can skip what landed.
     *
     * @param source: the name the progress is kept under
     * @return the highest sequence already applied, 0 when the source is unseen
     */
    virtual uint64_t resume_at(std::string_view source) = 0;

    /**
     * Queues the progress of one source, without completing it: the flush that
     * completes the increments it belongs with completes this too.
     *
     * @param source: the name the progress is kept under
     * @param seq: the highest sequence applied so far
     * @return false when the write could not be queued
     */
    virtual bool mark(std::string_view source, uint64_t seq) = 0;
};

} // namespace cdrp

