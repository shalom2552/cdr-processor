#include "query/services/rank_service.hpp"

#include "util/json.hpp"
#include "constants.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace cdrp {

/* The board names the route takes, under the keys they are kept in */
static constexpr std::pair<std::string_view, std::string_view> kBoards[] = {
    { "voice",    kVoiceBoard   },
    { "sms",      kSmsBoard     },
    { "data",     kDataBoard    },
    { "fail",     kFailBoard    },
    { "op-voice", kOpVoiceBoard },
    { "op-sms",   kOpSmsBoard   },
};

RankService::RankService(const IQueryStore& store)
    : m_store(store)
{
}

std::string_view RankService::keyOf(std::string_view board)
{
    for (const auto& [name, key] : kBoards) {
        if (name == board) {
            return key;
        }
    }
    return {};
}

Result RankService::top(std::string_view board, const QueryParams& params) const
{
    const std::string_view key = keyOf(board);
    if (key.empty()) {
        return { 400, Json::error("no such board") };
    }

    IQueryStore::Ranked ranked;
    uint64_t count = 0;
    if (!m_store.top(key, params.offset, params.limit, ranked, count)) {
        return { 503, Json::error("store unavailable") };
    }

    std::vector<Json> entries;
    entries.reserve(ranked.size());
    for (const auto& [id, score] : ranked) {
        entries.emplace_back(Json().add("id", id).add("score", score));
    }

    Json json;
    json.add("board", board)
        .add("count", count)
        .add("offset", params.offset)
        .add("limit", params.limit)
        .add("entries", entries);

    return { 200, json.str() };
}

} // namespace cdrp
