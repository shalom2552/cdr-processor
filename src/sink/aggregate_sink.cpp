#include "sink/aggregate_sink.hpp"

#include "cdr_record.hpp"
#include "aggregate/delta.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cdrp {

namespace {

/* The store behind the pointer, refused before the writer can bind a null one */
IStore& checked(const std::unique_ptr<IStore>& store)
{
    if (!store) {
        throw std::invalid_argument("AggregateSink: store must not be null");
    }
    return *store;
}

} // namespace

AggregateSink::AggregateSink(std::unique_ptr<IStore> store)
    : m_store(std::move(store))
    , m_writer(checked(m_store))
    , m_ranks(checked(m_store))
{
}

void AggregateSink::consume(std::vector<CdrRecord>& batch, std::string_view source)
{
    thread_local Delta delta;
    Totals batchTotals;

    m_aggregator.fold(batch, delta);
    batchTotals.add(batch);
    m_totals.merge(batchTotals);

    uint64_t highest = 0;
    for (const CdrRecord& record : batch) {
        highest = std::max(highest, record.sequence);
    }

    m_writer.write(batchTotals);
    if (!source.empty() && highest != 0) {
        m_store->mark(source, highest); // commits with the counters below
    }
    m_ranks.write(delta);
    m_writer.write(delta); // its flush closes the transaction over all of it
}

uint64_t AggregateSink::resume_at(std::string_view source)
{
    return m_store->resume_at(source);
}

Totals AggregateSink::snapshot() const
{
    return m_totals.snapshot();
}


} // namespace cdrp

