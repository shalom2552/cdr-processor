#include "query/query_params.hpp"

#include "util/json.hpp"
#include "constants.hpp"

#include "httplib.h"
#include <algorithm>
#include <charconv>
#include <string>

namespace cdrp {

/* One parameter of the request, empty when it names none */
static std::string param(const httplib::Request& req, const std::string& name)
{
    const auto found = req.params.find(name);
    return found == req.params.end() ? std::string() : found->second;
}

/* The whole text as a count, false when anything else is in it */
static bool count_of(const std::string& text, std::size_t& out)
{
    const char* const end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, out);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

/* The 400 a refused parameter sends */
static Result refused(const std::string& name)
{
    return { 400, Json::error("bad " + name) };
}

Result parseParams(const httplib::Request& req, const std::size_t fallback, const std::size_t cap,
                   QueryParams& out)
{
    out = QueryParams();

    const std::string weights = param(req, "weights");
    if (!weights.empty()) {
        if (weights != "0" && weights != "1") {
            return refused("weights");
        }
        out.weights = weights == "1";
    }

    const std::string sort = param(req, "sort");
    if (!sort.empty()) {
        if (sort != kSortDur && sort != kSortSms) {
            return refused("sort");
        }
        out.sort = sort == kSortSms ? Sort::Sms : Sort::Duration;
    }

    const std::string offset = param(req, "offset");
    if (!offset.empty() && !count_of(offset, out.offset)) {
        return refused("offset");
    }

    const std::string limit = param(req, "limit");
    if (limit.empty()) {
        out.limit = fallback;
    } else if (!count_of(limit, out.limit)) {
        return refused("limit");
    }
    out.limit = std::min(out.limit, cap);

    return {};
}

} // namespace cdrp
