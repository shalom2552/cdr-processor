#include "sink/aggregate_sink.hpp"

#include "cdr_record.hpp"
#include "aggregate/delta.hpp"

#include <memory>
#include <stdexcept>
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
{
}

void AggregateSink::consume(std::vector<CdrRecord>& batch)
{
    thread_local Delta delta;
    Totals batchTotals;

    m_aggregator.fold(batch, delta);
    batchTotals.add(batch);
    m_totals.merge(batchTotals);

    m_writer.write(batchTotals);
    m_writer.write(delta);
}

Totals AggregateSink::snapshot() const
{
    return m_totals.snapshot();
}


} // namespace cdrp

