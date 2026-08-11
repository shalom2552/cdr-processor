#pragma once

#include "query/iquery_store.hpp"
#include "query/query_params.hpp"
#include "query/result.hpp"

#include <string_view>

namespace cdrp {

/**
 * Answers one page of a ranking out of a store.
 * Knows which boards exist and the key each of them is kept under, nothing of HTTP.
 * Stateless: safe to share between threads if the store is.
 */
class RankService {
public:
    /**
     * Constructor.
     * @param store: the store every board is read from
     */
    explicit RankService(const IQueryStore& store);

    /* One page of a board, highest score first, 400 for a board that does not exist */
    Result top(std::string_view board, const QueryParams& params) const;

private:
    /* The key a board name is kept under, empty for a name that is not a board */
    static std::string_view keyOf(std::string_view board);

private:
    const IQueryStore& m_store;
};

} // namespace cdrp
