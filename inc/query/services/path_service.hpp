#pragma once

#include "query/iquery_store.hpp"
#include "query/result.hpp"
#include "util/json.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cdrp {

/**
 * Finds a path between two subscribers over the links in a store.
 * Searches from both ends at once, bounded by the two limits in config.
 * Stateless: safe to share between threads if the store is.
 */
class PathService {
public:
    /**
     * Constructor.
     * @param store: the store every link is read from
     */
    explicit PathService(const IQueryStore& store);

    /**
     * The subscribers along a path between two.
     *
     * @param first: the subscriber the search starts from
     * @param second: the subscriber the search ends at
     * @param weights: whether to report what each hop carried
     * @return the path, 404 with the bounds it gave up at when none was found
     */
    Result path(std::string_view first, std::string_view second, bool weights) const;

private:
    /* A subscriber to the one it was reached from, the root mapped to itself */
    using Trail = std::unordered_map<std::string, std::string>;

    /**
     * Joins the two halves of a met search into one ordered path.
     *
     * @param head: the trail grown from the first party
     * @param tail: the trail grown from the second party
     * @param meet: the subscriber both trails hold
     * @return the subscribers from the first party to the second, both included
     */
    static std::vector<std::string> walk(const Trail& head, const Trail& tail, const std::string& meet);

    /* What each hop of a route carried, zeros for a hop that reads empty */
    bool hops(const std::vector<std::string>& route, std::vector<Json>& out) const;

    /* The body a found path sends, its hops in it when they were asked for */
    Result found(const std::vector<std::string>& route, bool weights) const;

    /* The body a search that gave up sends, the bounds it gave up at in it */
    static std::string notFound();

private:
    const IQueryStore& m_store;
};

} // namespace cdrp
