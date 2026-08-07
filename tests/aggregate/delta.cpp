#include "doctest.h"
#include "aggregate/delta.hpp"

#include <cstdint>
#include <unordered_set>

using namespace cdrp;

TEST_CASE("link_keys_compare_field_by_field")
{
    const LinkKey key { 10, 20 };

    CHECK(key == LinkKey { 10, 20 });
    CHECK_FALSE(key == LinkKey { 10, 21 });
    CHECK_FALSE(key == LinkKey { 11, 20 });
    CHECK_FALSE(key == LinkKey { 0, 0 });
}

TEST_CASE("a_link_key_is_directed")
{
    const LinkKey forward { 425020528409010ULL, 262040162782277ULL };
    const LinkKey backward { 262040162782277ULL, 425020528409010ULL };

    CHECK_FALSE(forward == backward);
    CHECK(LinkHash {}(forward) != LinkHash {}(backward));
}

TEST_CASE("link_hash_gives_equal_keys_the_same_value")
{
    const LinkHash hash;

    CHECK(hash(LinkKey {}) == hash(LinkKey { 0, 0 }));
    CHECK(hash(LinkKey { 425020528409010ULL, 972528409042ULL })
        == hash(LinkKey { 425020528409010ULL, 972528409042ULL }));
}

TEST_CASE("link_hash_separates_neighbouring_keys")
{
    const LinkHash hash;
    std::unordered_set<std::size_t> seen;

    for (uint64_t owner = 0; owner < 100; ++owner) {
        for (uint64_t peer = 0; peer < 100; ++peer) {
            seen.insert(hash(LinkKey { owner, peer }));
        }
    }

    CHECK(seen.size() == 100 * 100);
}
