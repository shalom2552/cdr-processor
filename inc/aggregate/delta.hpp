#pragma once

#include <cstdint>
#include <unordered_map>

namespace cdrp {

using SubKey_t = uint64_t; // subscriber MSISDN
using OpKey_t = uint32_t;  // operator code, MCC and MNC together

/* one subscriber's usage over a batch */
struct SubDelta {
    uint64_t voice_out = 0; // outgoing call seconds
    uint64_t voice_in = 0;  // incoming call seconds
    uint64_t data_rx = 0;   // bytes received
    uint64_t data_tx = 0;   // bytes transmitted
    uint64_t sms_out = 0;
    uint64_t sms_in = 0;
    uint64_t noans = 0;  // U
    uint64_t busy = 0;   // B
    uint64_t failed = 0; // X
};

/* one operator's voice and sms traffic over a batch */
struct OpDelta {
    uint64_t voice_out = 0;
    uint64_t voice_in = 0;
    uint64_t sms_out = 0;
    uint64_t sms_in = 0;
};

/* what one pair of subscribers exchanged over a batch */
struct LinkDelta {
    uint64_t dur = 0; // call seconds exchanged
    uint64_t sms = 0; // messages exchanged
    uint64_t cnt = 0; // calls exchanged
};

/* a directed pair, owner to peer */
struct LinkKey {
    uint64_t owner = 0;
    uint64_t peer = 0;

    bool operator==(const LinkKey& other) const {
        return owner == other.owner && peer == other.peer;
    }
};

/* hashes a LinkKey, owner and peer swapped gives another value */
struct LinkHash {
    std::size_t operator()(const LinkKey& key) const noexcept {
        return mix(key.owner) ^ (mix(key.peer) + 0x9e3779b97f4a7c15ULL);
    }
private:
    /* splitmix64 finalizer */
    static uint64_t mix(uint64_t x) noexcept {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
};

using SubMap = std::unordered_map<SubKey_t, SubDelta>;
using OpMap = std::unordered_map<OpKey_t, OpDelta>;
using LinkMap = std::unordered_map<LinkKey, LinkDelta, LinkHash>;

/**
 * One batch of counters, kept by subscriber, by operator, and by pair.
 * The aggregator fills it, the sink writes it out and it is thrown away.
 */
struct Delta {
    SubMap subs;
    OpMap ops;
    LinkMap links;
};

} // namespace cdrp

