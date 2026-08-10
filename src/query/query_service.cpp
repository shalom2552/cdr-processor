#include "query/query_service.hpp"

#include "util/json.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include <algorithm>
#include <charconv>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static constexpr std::string_view kComponent = "QueryService";

namespace cdrp {

/* One stored value as a number, 0 when it does not parse */
static uint64_t to_num(std::string_view value)
{
    uint64_t out = 0;
    std::from_chars(value.data(), value.data() + value.size(), out);
    return out;
}

/* True for the byte counters, which are reported in KB */
static bool is_bytes(std::string_view field)
{
    return field == kFieldDataTx || field == kFieldDataRx;
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

QueryService::Result QueryService::msisdn(std::string_view msisdn) const
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
        const uint64_t num = field_num(fields, field);
        json.add(name, is_bytes(field) ? num / kBytesPerKb : num);
    }

    return { 200, json.str() };
}

QueryService::Result QueryService::op(std::string_view mccmnc) const
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

QueryService::Result QueryService::peers(std::string_view msisdn) const
{
    std::vector<std::string> out;
    if (!neighbours(msisdn, out)) {
        return { 503, Json::error("store unavailable") };
    }
    if (out.empty()) {
        return { 404, Json::error("subscriber not found") };
    }

    Json json;
    json.add("msisdn", msisdn).add("peers", out);
    return { 200, json.str() };
}

QueryService::Result QueryService::link(std::string_view first, std::string_view second) const
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

QueryService::Result QueryService::path(std::string_view first, std::string_view second) const
{
    const std::string from(first);
    const std::string to(second);

    if (from == to) {
        return { 200, Json().add("path", std::vector<std::string>{ from }).str() };
    }

    std::unordered_map<std::string, std::string> seen[2];
    std::deque<std::string> frontier[2];

    seen[0][from] = from;
    seen[1][to] = to;
    frontier[0].push_back(from);
    frontier[1].push_back(to);

    std::size_t visited = 0;

    for (std::size_t hop = 0; hop < kMaxHops; ++hop) {
        // Expand the narrower side, which keeps the search off the hubs for longer
        const std::size_t side = frontier[0].size() <= frontier[1].size() ? 0 : 1;
        const std::size_t width = frontier[side].size();

        for (std::size_t i = 0; i < width; ++i) {
            const std::string node = frontier[side].front();
            frontier[side].pop_front();

            std::vector<std::string> peers;
            if (!neighbours(node, peers)) {
                return { 503, Json::error("store unavailable") };
            }

            for (const auto& peer : peers) {
                if (seen[side].count(peer)) {
                    continue;
                }
                if (++visited > kMaxVisited) {
                    logWarn(kComponent, "path search gave up after " +
                                        std::to_string(visited) + " subscribers");
                    return { 404, Json::error("path not found") };
                }

                seen[side][peer] = node;

                if (seen[1 - side].count(peer)) {
                    return { 200, Json().add("path", walk(seen[0], seen[1], peer)).str() };
                }

                frontier[side].push_back(peer);
            }
        }

        if (frontier[side].empty()) {
            break;
        }
    }

    return { 404, Json::error("path not found") };
}

std::vector<std::string> QueryService::walk(const Trail& head, const Trail& tail, const std::string& meet)
{
    std::vector<std::string> path;

    // Back from the meeting point to the first party, which maps to itself
    for (std::string node = meet;; ) {
        path.push_back(node);
        const std::string& parent = head.at(node);
        if (parent == node) {
            break;
        }
        node = parent;
    }
    std::reverse(path.begin(), path.end());

    // On to the second party, the meeting point already in
    for (std::string node = tail.at(meet); ; ) {
        if (node == path.back()) {
            break;
        }
        path.push_back(node);
        const std::string& parent = tail.at(node);
        if (parent == node) {
            break;
        }
        node = parent;
    }

    return path;
}

bool QueryService::neighbours(std::string_view msisdn, std::vector<std::string>& out) const
{
    out.clear();

    std::vector<std::string> fields;
    if (!m_store.hkeys(std::string(kLinkPrefix) + std::string(msisdn), fields)) {
        return false;
    }

    // Every peer holds two fields, so the names are halved back into peers
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

} // namespace cdrp

