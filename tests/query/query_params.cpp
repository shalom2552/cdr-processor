#include "doctest.h"
#include "constants.hpp"
#include "query/query_params.hpp"

#include "httplib.h"
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace cdrp;

/* A request holding the parameters a test names */
httplib::Request asked(const std::vector<std::pair<std::string, std::string>>& params)
{
    httplib::Request req;
    for (const auto& [name, value] : params) {
        req.params.emplace(name, value);
    }
    return req;
}

} // namespace

using namespace cdrp;

TEST_CASE("query_params_gives_a_request_that_names_nothing_the_defaults")
{
    QueryParams params;

    const Result result = parseParams(asked({}), kPeerLimit, kPeerLimitMax, params);

    CHECK(result.status == 200);
    CHECK(result.body.empty());
    CHECK_FALSE(params.weights);
    CHECK(params.sort == Sort::Duration);
    CHECK(params.offset == 0);
    CHECK(params.limit == kPeerLimit);
}

TEST_CASE("query_params_reads_every_parameter_of_a_request")
{
    QueryParams params;

    const Result result = parseParams(
        asked({ { "weights", "1" }, { "sort", "sms" }, { "limit", "7" }, { "offset", "3" } }),
        kPeerLimit, kPeerLimitMax, params);

    CHECK(result.status == 200);
    CHECK(params.weights);
    CHECK(params.sort == Sort::Sms);
    CHECK(params.limit == 7);
    CHECK(params.offset == 3);
}

TEST_CASE("query_params_reads_weights_zero_as_no_weights")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "weights", "0" } }), kPeerLimit, kPeerLimitMax, params).status == 200);
    CHECK_FALSE(params.weights);
}

TEST_CASE("query_params_clamps_a_limit_over_the_cap")
{
    QueryParams params;

    const Result result = parseParams(asked({ { "limit", "5000" } }), kPeerLimit, kPeerLimitMax, params);

    CHECK(result.status == 200);
    CHECK(params.limit == kPeerLimitMax);
}

TEST_CASE("query_params_takes_the_fallback_it_was_handed")
{
    QueryParams params;

    CHECK(parseParams(asked({}), kTopLimit, kTopLimitMax, params).status == 200);
    CHECK(params.limit == kTopLimit);
}

TEST_CASE("query_params_refuses_a_sort_that_is_not_a_metric")
{
    QueryParams params;

    const Result result = parseParams(asked({ { "sort", "calls" } }), kPeerLimit, kPeerLimitMax, params);

    CHECK(result.status == 400);
    CHECK(result.body.find("sort") != std::string::npos);
}

TEST_CASE("query_params_refuses_a_limit_that_is_not_a_number")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "limit", "ten" } }), kPeerLimit, kPeerLimitMax, params).status == 400);
}

TEST_CASE("query_params_refuses_a_limit_with_a_number_only_at_its_start")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "limit", "10x" } }), kPeerLimit, kPeerLimitMax, params).status == 400);
}

TEST_CASE("query_params_refuses_an_offset_that_is_not_a_number")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "offset", "-1" } }), kPeerLimit, kPeerLimitMax, params).status == 400);
}

TEST_CASE("query_params_refuses_weights_that_is_neither_zero_nor_one")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "weights", "yes" } }), kPeerLimit, kPeerLimitMax, params).status == 400);
}

TEST_CASE("query_params_takes_the_two_sort_metrics_it_names")
{
    QueryParams params;

    CHECK(parseParams(asked({ { "sort", std::string(kSortDur) } }), kPeerLimit, kPeerLimitMax, params).status == 200);
    CHECK(params.sort == Sort::Duration);
    CHECK(parseParams(asked({ { "sort", std::string(kSortSms) } }), kPeerLimit, kPeerLimitMax, params).status == 200);
    CHECK(params.sort == Sort::Sms);
}
