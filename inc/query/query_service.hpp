#pragma once

#include "query/iquery_store.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cdrp {

/**
 * Answers the gateway's queries out of a store.
 * Knows the key and field names of the aggregates and the shape of the responses,
 * nothing of HTTP. Stateless: safe to share between threads if the store is.
 */
class QueryService {
public:
    /* A response: the status to send and the JSON body to send with it */
    struct Result {
        int status = 200;
        std::string body;
    };

    /**
     * Constructor.
     * @param store: the store every query is read from
     */
    explicit QueryService(const IQueryStore& store);

    /* One subscriber's usage, 404 when the subscriber was never seen */
    Result msisdn(std::string_view msisdn) const;

    /* One operator's voice and sms traffic, 404 when the operator was never seen */
    Result op(std::string_view mccmnc) const;

    /* Every peer one subscriber exchanged calls or messages with, 404 when it has none */
    Result peers(std::string_view msisdn) const;

    /* What one pair exchanged, 404 when they never were in contact */
    Result link(std::string_view first, std::string_view second) const;

    /* The subscribers along a path between two, 404 when none was found */
    Result path(std::string_view first, std::string_view second) const;

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

    /* Every peer of one subscriber, its link fields stripped of their suffix */
    bool neighbours(std::string_view msisdn, std::vector<std::string>& out) const;

private:
    const IQueryStore& m_store;
};

} // namespace cdrp
