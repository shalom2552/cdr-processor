#include "query/services/query_service.hpp"

#include "query/links.hpp"
#include "util/json.hpp"
#include "constants.hpp"

#include <charconv>
#include <string>
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

/* The stored fields a subscriber is reported with, under the names they go out as */
static constexpr std::pair<std::string_view, std::string_view> kSubFields[] = {
    { kFieldVoiceOut, kJsonVoiceOut },
    { kFieldVoiceIn,  kJsonVoiceIn  },
    { kFieldDataTx,   kJsonDataOut  },
    { kFieldDataRx,   kJsonDataIn   },
    { kFieldSmsOut,   kJsonSmsOut   },
    { kFieldSmsIn,    kJsonSmsIn    },
    { kFieldNoans,    kJsonNoans    },
    { kFieldBusy,     kJsonBusy     },
    { kFieldFailed,   kJsonFailed   },
};

/* The stored fields an operator is reported with, under the names they go out as */
static constexpr std::pair<std::string_view, std::string_view> kOpFields[] = {
    { kFieldVoiceOut, kJsonVoiceOut },
    { kFieldVoiceIn,  kJsonVoiceIn  },
    { kFieldSmsOut,   kJsonSmsOut   },
    { kFieldSmsIn,    kJsonSmsIn    },
};

/* One field of what the store returned as a number, 0 when the key does not hold it */
static uint64_t field_num(const IQueryStore::Fields& fields, std::string_view field)
{
    for (const auto& [key, value] : fields) {
        if (key == field) {
            return to_num(value);
        }
    }
    return 0;
}

QueryService::QueryService(const IQueryStore& store)
    : m_store(store)
{
}

Result QueryService::msisdn(std::string_view msisdn) const
{
    IQueryStore::Fields fields;
    if (!m_store.hgetall(std::string(kSubPrefix) + std::string(msisdn), fields)) {
        return { 503, Json::error("store unavailable") };
    }
    if (fields.empty()) {
        return { 404, Json::error("subscriber not found") };
    }

    Json json;
    json.add("msisdn", msisdn);
    for (const auto& [field, name] : kSubFields) {
        json.add(name, field_num(fields, field));
    }

    return { 200, json.str() };
}

Result QueryService::op(std::string_view mccmnc) const
{
    IQueryStore::Fields fields;
    if (!m_store.hgetall(std::string(kOpPrefix) + std::string(mccmnc), fields)) {
        return { 503, Json::error("store unavailable") };
    }
    if (fields.empty()) {
        return { 404, Json::error("operator not found") };
    }

    Json json;
    json.add("mccmnc", mccmnc);
    for (const auto& [field, name] : kOpFields) {
        json.add(name, field_num(fields, field));
    }

    return { 200, json.str() };
}

Result QueryService::peers(std::string_view msisdn, const QueryParams& params) const
{
    if (params.weights) {
        return weighted(msisdn, params);
    }

    std::vector<std::string> all;
    if (!link_peers(m_store, msisdn, all)) {
        return { 503, Json::error("store unavailable") };
    }
    if (all.empty()) {
        return { 404, Json::error("subscriber not found") };
    }

    Json json;
    json.add("msisdn", msisdn)
        .add("count", all.size())
        .add("offset", params.offset)
        .add("limit", params.limit)
        .add("peers", page(all, params));

    return { 200, json.str() };
}

Result QueryService::weighted(std::string_view msisdn, const QueryParams& params) const
{
    std::vector<Peer> all;
    if (!link_weights(m_store, msisdn, all)) {
        return { 503, Json::error("store unavailable") };
    }
    if (all.empty()) {
        return { 404, Json::error("subscriber not found") };
    }

    order_peers(all, params.sort);

    std::vector<Json> peers;
    for (const Peer& peer : page(all, params)) {
        peers.emplace_back(Json().add("msisdn", peer.msisdn)
                                 .add(kJsonDuration, peer.duration)
                                 .add(kJsonSms, peer.sms));
    }

    Json json;
    json.add("msisdn", msisdn)
        .add("count", all.size())
        .add("offset", params.offset)
        .add("limit", params.limit)
        .add("sort", params.sort == Sort::Sms ? kSortSms : kSortDur)
        .add("peers", peers);

    return { 200, json.str() };
}

Result QueryService::link(std::string_view first, std::string_view second) const
{
    const std::vector<std::string> fields = {
        std::string(second) + std::string(kFieldDurSuffix),
        std::string(second) + std::string(kFieldSmsSuffix),
    };

    std::vector<std::string> values;
    if (!m_store.hmget(std::string(kLinkPrefix) + std::string(first), fields, values)) {
        return { 503, Json::error("store unavailable") };
    }
    if (values.size() != 2 || (values[0].empty() && values[1].empty())) {
        return { 404, Json::error("link not found") };
    }

    Json json;
    json.add("first-party", first)
        .add("second-party", second)
        .add(kJsonDuration, to_num(values[0]))
        .add(kJsonSms, to_num(values[1]));

    return { 200, json.str() };
}

} // namespace cdrp
