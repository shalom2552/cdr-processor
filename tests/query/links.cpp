#include "doctest.h"
#include "fake_store.hpp"
#include "constants.hpp"
#include "query/links.hpp"
#include "query/query_params.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace cdrp;

/* The subscriber whose link key the tests read */
const std::string kSubject = "972500000001";

/* Its peers, named so the ascending order is not the order they are written in */
const std::string kLowPeer = "972500000002";
const std::string kHighPeer = "972500000007";

/* The subscriber no link key was written for */
const std::string kStranger = "972500000009";

/* One peer of the subject, each metric named on its own */
void put(FakeStore& store, const std::string& peer, const std::string& dur, const std::string& sms,
         const std::string& cnt)
{
    const std::string key = std::string(kLinkPrefix) + kSubject;
    store.put(key, peer + std::string(kFieldDurSuffix), dur);
    store.put(key, peer + std::string(kFieldSmsSuffix), sms);
    store.put(key, peer + std::string(kFieldCntSuffix), cnt);
}

/* One peer to order, its msisdn built from the number it is given */
Peer peer(const std::string& msisdn, uint64_t duration, uint64_t sms)
{
    Peer out;
    out.msisdn = msisdn;
    out.duration = duration;
    out.sms = sms;
    return out;
}

/* The msisdns of peers, in the order they are held */
std::vector<std::string> namesOf(const std::vector<Peer>& peers)
{
    std::vector<std::string> out;
    for (const Peer& one : peers) {
        out.push_back(one.msisdn);
    }
    return out;
}

/* The peer of one msisdn, so a test can name what it carried */
Peer found(const std::vector<Peer>& peers, const std::string& msisdn)
{
    for (const Peer& one : peers) {
        if (one.msisdn == msisdn) {
            return one;
        }
    }
    return Peer();
}

} // namespace

using namespace cdrp;

TEST_CASE("link_peers_reads_every_peer_once_ascending_with_the_suffixes_stripped")
{
    FakeStore store;
    put(store, kHighPeer, "90", "5", "4");
    put(store, kLowPeer, "60", "3", "2");
    std::vector<std::string> peers;

    REQUIRE(link_peers(store, kSubject, peers));

    CHECK(peers == std::vector<std::string> { kLowPeer, kHighPeer });
}

TEST_CASE("link_peers_reads_no_peers_for_a_subscriber_that_has_no_link_key")
{
    FakeStore store;
    put(store, kLowPeer, "60", "3", "2");
    std::vector<std::string> peers { "stale" };

    CHECK(link_peers(store, kStranger, peers));
    CHECK(peers.empty());
}

TEST_CASE("link_peers_fails_when_the_store_cannot_be_read")
{
    FakeStore store;
    put(store, kLowPeer, "60", "3", "2");
    store.storeUp = false;
    std::vector<std::string> peers;

    CHECK_FALSE(link_peers(store, kSubject, peers));
}

TEST_CASE("link_weights_reads_each_field_onto_the_member_it_belongs_to")
{
    FakeStore store;
    put(store, kLowPeer, "60", "3", "2");
    std::vector<Peer> peers;

    REQUIRE(link_weights(store, kSubject, peers));

    REQUIRE(peers.size() == 1);
    CHECK(peers[0].msisdn == kLowPeer);
    CHECK(peers[0].duration == 60);
    CHECK(peers[0].sms == 3);
    CHECK(peers[0].calls == 2);
}

TEST_CASE("link_weights_reads_a_peer_that_only_sent_sms")
{
    FakeStore store;
    put(store, kLowPeer, "60", "3", "2");
    store.put(std::string(kLinkPrefix) + kSubject, kHighPeer + std::string(kFieldSmsSuffix), "7");
    std::vector<Peer> peers;

    REQUIRE(link_weights(store, kSubject, peers));

    REQUIRE(peers.size() == 2);
    const Peer only = found(peers, kHighPeer);
    CHECK(only.msisdn == kHighPeer);
    CHECK(only.sms == 7);
    CHECK(only.duration == 0);
}

TEST_CASE("order_peers_orders_by_duration_descending")
{
    std::vector<Peer> peers { peer("1", 10, 90), peer("2", 90, 10), peer("3", 50, 50) };

    order_peers(peers, Sort::Duration);

    CHECK(namesOf(peers) == std::vector<std::string> { "2", "3", "1" });
}

TEST_CASE("order_peers_orders_by_sms_descending")
{
    std::vector<Peer> peers { peer("1", 90, 10), peer("2", 10, 90), peer("3", 50, 50) };

    order_peers(peers, Sort::Sms);

    CHECK(namesOf(peers) == std::vector<std::string> { "2", "3", "1" });
}

TEST_CASE("order_peers_breaks_a_tie_on_the_other_metric_and_then_on_msisdn")
{
    std::vector<Peer> peers { peer("3", 60, 1), peer("1", 60, 1), peer("2", 60, 9) };

    order_peers(peers, Sort::Duration);

    CHECK(namesOf(peers) == std::vector<std::string> { "2", "1", "3" });
}

TEST_CASE("order_peers_breaks_a_sms_tie_on_duration_and_then_on_msisdn")
{
    std::vector<Peer> peers { peer("3", 1, 60), peer("1", 1, 60), peer("2", 9, 60) };

    order_peers(peers, Sort::Sms);

    CHECK(namesOf(peers) == std::vector<std::string> { "2", "1", "3" });
}
