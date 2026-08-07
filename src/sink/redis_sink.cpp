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

    m_aggregator.fold(batch, delta);
    m_total.fetch_add(batch.size(), std::memory_order_relaxed);

    m_writer.write(delta);
}

std::size_t RedisSink::total() const
{
    return m_total.load(std::memory_order_relaxed); 
}


} // namespace cdrp

