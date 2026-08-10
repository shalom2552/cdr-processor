#pragma once

#include "query/result.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace httplib { struct Request; }

namespace cdrp {

/* The metric a weighted listing is ordered by */
enum class Sort { Duration, Sms };

/**
 * The optional parameters a listing route takes, already checked.
 * A limit of 0 asks for every entry, which is what a caller that named none gets.
 */
struct QueryParams {
    bool weights = false;
    Sort sort = Sort::Duration;
    std::size_t offset = 0;
    std::size_t limit = 0;
};

/**
 * Reads the parameters off one request, clamping a limit over the cap.
 *
 * @param req: the request holding the query string
 * @param fallback: the limit a request that names none is given
 * @param cap: the largest limit a request may ask for
 * @param out: filled with what the request holds, defaults where it holds nothing
 * @return 200 with an empty body when it parsed, 400 with the reason when it did not
 */
Result parseParams(const httplib::Request& req, std::size_t fallback, std::size_t cap,
                   QueryParams& out);

/**
 * The page of a listing the parameters ask for.
 *
 * @param all: every entry, in the order they go out
 * @param params: the offset and limit to cut the page by
 * @return the entries of the page, empty when the offset is past the end
 */
template <typename T>
std::vector<T> page(const std::vector<T>& all, const QueryParams& params)
{
    if (params.offset >= all.size()) {
        return {};
    }

    const std::size_t left = all.size() - params.offset;
    const std::size_t taken = params.limit == 0 ? left : std::min(params.limit, left);
    const auto begin = all.begin() + static_cast<std::ptrdiff_t>(params.offset);

    return std::vector<T>(begin, begin + static_cast<std::ptrdiff_t>(taken));
}

} // namespace cdrp
