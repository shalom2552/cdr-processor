#pragma once

#include "query/iquery_store.hpp"
#include "query/query_params.hpp"
#include "query/result.hpp"

#include <string_view>

namespace cdrp {

/**
 * Answers the gateway's entity lookups out of a store.
 * Knows the key and field names of the aggregates and the shape of the responses,
 * nothing of HTTP. Stateless: safe to share between threads if the store is.
 */
class QueryService {
public:
    /**
     * Constructor.
     * @param store: the store every query is read from
     */
    explicit QueryService(const IQueryStore& store);

    /* One subscriber's usage, 404 when the subscriber was never seen */
    Result msisdn(std::string_view msisdn) const;

    /* One operator's voice and sms traffic, 404 when the operator was never seen */
    Result op(std::string_view mccmnc) const;

    /**
     * One page of the peers one subscriber exchanged calls or messages with.
     *
     * @param msisdn: the subscriber whose peers are read
     * @param params: what to weigh, order and page by
     * @return the peers, 404 when the subscriber has none
     */
    Result peers(std::string_view msisdn, const QueryParams& params) const;

    /* What one pair exchanged, 404 when they never were in contact */
    Result link(std::string_view first, std::string_view second) const;

private:
    /* The peers of one subscriber with what each of them exchanged */
    Result weighted(std::string_view msisdn, const QueryParams& params) const;

private:
    const IQueryStore& m_store;
};

} // namespace cdrp
