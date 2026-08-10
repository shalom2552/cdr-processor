#pragma once

#include "query/iquery_store.hpp"
#include "query/query_params.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cdrp {

/* One peer with what it exchanged with the subscriber it belongs to */
struct Peer {
    std::string msisdn;
    uint64_t duration = 0;
    uint64_t sms = 0;
};

/**
 * Every peer of one subscriber, its link fields stripped of their suffix.
 *
 * @param store: the store the link key is read from
 * @param msisdn: the subscriber whose peers are read
 * @param out: filled with the peer names ascending, cleared first
 * @return false when the read failed
 */
bool link_peers(const IQueryStore& store, std::string_view msisdn, std::vector<std::string>& out);

/**
 * Every peer of one subscriber with what the pair exchanged.
 *
 * @param store: the store the link key is read from
 * @param msisdn: the subscriber whose peers are read
 * @param out: filled with the peers, unordered, cleared first
 * @return false when the read failed
 */
bool link_weights(const IQueryStore& store, std::string_view msisdn, std::vector<Peer>& out);

/**
 * Orders peers by one metric descending.
 *
 * @param peers: the peers to order, in place
 * @param sort: the metric to order by, ties by the other one and then by msisdn
 */
void order_peers(std::vector<Peer>& peers, Sort sort);

} // namespace cdrp
