#include "query/services/path_service.hpp"

#include "query/links.hpp"
#include "util/json.hpp"
#include "config.hpp"
#include "constants.hpp"
#include "logger.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr std::string_view kComponent = "PathService";

namespace cdrp {

/* One stored value as a number, 0 when it does not parse */
static uint64_t to_num(std::string_view value)
{
    uint64_t out = 0;
    std::from_chars(value.data(), value.data() + value.size(), out);
    return out;
}

PathService::PathService(const IQueryStore& store)
    : m_store(store)
{
}

Result PathService::path(std::string_view first, std::string_view second, const bool weights) const
{
    const std::string from(first);
    const std::string to(second);

    if (from == to) {
        return found({ from }, weights);
    }

    Trail seen[2];
    std::deque<std::string> frontier[2];

    seen[0][from] = from;
    seen[1][to] = to;
    frontier[0].push_back(from);
    frontier[1].push_back(to);

    std::size_t visited = 0;

    for (std::size_t hop = 0; hop < cfg.query.max_hops; ++hop) {
        // Expand the narrower side, which keeps the search off the hubs for longer
        const std::size_t side = frontier[0].size() <= frontier[1].size() ? 0 : 1;
        const std::size_t width = frontier[side].size();

        for (std::size_t i = 0; i < width; ++i) {
            const std::string node = frontier[side].front();
            frontier[side].pop_front();

            std::vector<std::string> peers;
            if (!link_peers(m_store, node, peers)) {
                return { 503, Json::error("store unavailable") };
            }

            for (const auto& peer : peers) {
                if (seen[side].count(peer)) {
                    continue;
                }
                if (++visited > cfg.query.max_visited) {
                    logWarn(kComponent, "path search gave up after " +
                                        std::to_string(visited) + " subscribers");
                    return { 404, notFound() };
                }

                seen[side][peer] = node;

                if (seen[1 - side].count(peer)) {
                    return found(walk(seen[0], seen[1], peer), weights);
                }

                frontier[side].push_back(peer);
            }
        }

        if (frontier[side].empty()) {
            break;
        }
    }

    return { 404, notFound() };
}

std::vector<std::string> PathService::walk(const Trail& head, const Trail& tail, const std::string& meet)
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

bool PathService::hops(const std::vector<std::string>& route, std::vector<Json>& out) const
{
    out.clear();

    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        const std::vector<std::string> fields = {
            route[i + 1] + std::string(kFieldDurSuffix),
            route[i + 1] + std::string(kFieldSmsSuffix),
            route[i + 1] + std::string(kFieldCntSuffix),
        };

        std::vector<std::string> values;
        if (!m_store.hmget(std::string(kLinkPrefix) + route[i], fields, values)) {
            return false;
        }

        values.resize(fields.size()); // a hop that reads empty carried nothing
        out.emplace_back(Json().add("from", route[i])
                               .add("to", route[i + 1])
                               .add(kJsonDuration, to_num(values[0]))
                               .add(kJsonCalls, to_num(values[2]))
                               .add(kJsonSms, to_num(values[1])));
    }

    return true;
}

Result PathService::found(const std::vector<std::string>& route, const bool weights) const
{
    Json json;
    json.add("path", route);

    if (weights) {
        std::vector<Json> carried;
        if (!hops(route, carried)) {
            return { 503, Json::error("store unavailable") };
        }
        json.add("hops", carried);
    }

    return { 200, json.str() };
}

std::string PathService::notFound()
{
    return Json().add("error", "path not found")
                 .add(kJsonMaxHops, cfg.query.max_hops)
                 .add(kJsonMaxVisited, cfg.query.max_visited)
                 .str();
}

} // namespace cdrp
