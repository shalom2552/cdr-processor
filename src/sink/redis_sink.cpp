#include "sink/redis_sink.hpp"

#include "cdr_record.hpp"
#include "aggregate/delta.hpp"

#include <vector>

namespace cdrp {

RedisSink::RedisSink()
    : m_writer(m_store)
{
}

void RedisSink::consume(std::vector<CdrRecord>& batch)
{
    thread_local Delta delta;
    Totals batchTotals;

    m_aggregator.fold(batch, delta);
    batchTotals.add(batch);
    m_totals.merge(batchTotals);

    m_writer.write(batchTotals);
    m_writer.write(delta);
}

Totals RedisSink::snapshot() const
{
    return m_totals.snapshot();
}


} // namespace cdrp

