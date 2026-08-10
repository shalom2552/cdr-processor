#include "query/links.hpp"

#include "constants.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cdrp {

/* One stored value as a number, 0 when it does not parse */
static uint64_t to_num(std::string_view value)
{
    uint64_t out = 0;
    std::from_chars(value.data(), value.data() + value.size(), out);
    return out;
}

/* The key one subscriber's links are kept under */
static std::string link_key(std::string_view msisdn)
{
    return std::string(kLinkPrefix) + std::string(msisdn);
}

bool link_peers(const IQueryStore& store, std::string_view msisdn, std::vector<std::string>& out)
{
    out.clear();

    std::vector<std::string> fields;
    if (!store.hkeys(link_key(msisdn), fields)) {
        return false;
    }

    // Every peer holds one field per metric, so the names are cut back to the peer
    for (auto& field : fields) {
        const std::size_t colon = field.rfind(':');
        if (colon != std::string::npos) {
            field.resize(colon);
        }
    }

    std::sort(fields.begin(), fields.end());
    fields.erase(std::unique(fields.begin(), fields.end()), fields.end());
    out = std::move(fields);

    return true;
}

bool link_weights(const IQueryStore& store, std::string_view msisdn, std::vector<Peer>& out)
{
    out.clear();

    IQueryStore::Fields fields;
    if (!store.hgetall(link_key(msisdn), fields)) {
        return false;
    }

    std::unordered_map<std::string, Peer> byMsisdn;
    for (const auto& [field, value] : fields) {
        const std::size_t colon = field.rfind(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string name = field.substr(0, colon);
        Peer& peer = byMsisdn[name];
        peer.msisdn = name;
        if (field.compare(colon, std::string::npos, kFieldSmsSuffix) == 0) {
            peer.sms = to_num(value);
        } else if (field.compare(colon, std::string::npos, kFieldCntSuffix) == 0) {
            peer.calls = to_num(value);
        } else {
            peer.duration = to_num(value);
        }
    }

    out.reserve(byMsisdn.size());
    for (auto& [name, peer] : byMsisdn) {
        out.push_back(std::move(peer));
    }

    return true;
}

void order_peers(std::vector<Peer>& peers, const Sort sort)
{
    std::sort(peers.begin(), peers.end(), [sort](const Peer& left, const Peer& right) {
        const uint64_t leftBy = sort == Sort::Sms ? left.sms : left.duration;
        const uint64_t rightBy = sort == Sort::Sms ? right.sms : right.duration;
        if (leftBy != rightBy) {
            return leftBy > rightBy;
        }

        // ties by the other metric, then by msisdn, so paging is stable
        const uint64_t leftTie = sort == Sort::Sms ? left.duration : left.sms;
        const uint64_t rightTie = sort == Sort::Sms ? right.duration : right.sms;
        if (leftTie != rightTie) {
            return leftTie > rightTie;
        }
        return left.msisdn < right.msisdn;
    });
}

} // namespace cdrp
