#include "query/services/stats_service.hpp"

#include "util/json.hpp"
#include "config.hpp"
#include "constants.hpp"

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>

namespace cdrp {

/* The stored fields the totals are reported with, in the order they go out */
static constexpr std::string_view kTotalFields[] = {
    kFieldRecords, kFieldMocCnt, kFieldMtcCnt, kFieldSmsMoCnt, kFieldSmsMtCnt,
    kFieldDataCnt, kFieldNoansCnt, kFieldBusyCnt, kFieldFailedCnt,
    kFieldMocDur, kFieldMtcDur, kFieldDataDur, kFieldDataRx, kFieldDataTx,
};

/* One stored field name under the name it goes out as */
static std::string reported(std::string_view field)
{
    std::string name(field);
    std::replace(name.begin(), name.end(), '_', '-');
    return name;
}

/* One field of what the store returned as a number, 0 when the key does not hold it */
static uint64_t field_num(const IQueryStore::Fields& fields, std::string_view field)
{
    for (const auto& [key, value] : fields) {
        if (key == field) {
            uint64_t out = 0;
            std::from_chars(value.data(), value.data() + value.size(), out);
            return out;
        }
    }
    return 0;
}

StatsService::StatsService(const IQueryStore& store)
    : m_store(store)
{
}

Result StatsService::health() const
{
    uint64_t keys = 0;
    const bool up = m_store.dbsize(keys);

    Json json;
    json.add("status", "ok")
        .add("store", up ? "up" : "down")
        .add("keys", up ? keys : 0)
        .add(kJsonMaxHops, cfg.query.max_hops)
        .add(kJsonMaxVisited, cfg.query.max_visited);

    return { 200, json.str() };
}

Result StatsService::totals() const
{
    IQueryStore::Fields fields;
    if (!m_store.hgetall(kTotalKey, fields)) {
        return { 503, Json::error("store unavailable") };
    }

    Json json;
    for (const std::string_view field : kTotalFields) {
        json.add(reported(field), field_num(fields, field));
    }

    return { 200, json.str() };
}

} // namespace cdrp
