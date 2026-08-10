#include "source/rabbit_source.hpp"

#include "cdr_record.hpp"
#include "logger.hpp"
#include "source/rabbit_conn.hpp"
#include "parser/parser_factory.hpp"
#include "constants.hpp"
#include "config.hpp"

#include <stdexcept>

constexpr std::string_view kComponent = "RabbitSource";

namespace cdrp {


RabbitSource::RabbitSource(RabbitConn& connection)
    : m_conn(connection)
    , m_parser(ParserFactory::instance().createParser(cfg.source.format))
{
    if (!m_parser) {
        logError(kComponent, "Failed to create parser");
        throw std::runtime_error("Failed to create parser");
    }

    logInfo(kComponent, "consuming with parser: " + cfg.source.format);
}

RabbitSource::Status RabbitSource::next(std::vector<CdrRecord>& out)
{
    out.clear();

    while (!m_stop && out.size() < kRabbitBatchSize) {
        RabbitConn::Message msg;
        const auto st = m_conn.consume(msg, kPollMs);

        if (st == RabbitConn::Status::TIMEOUT) {
            break;
        }
        if (st == RabbitConn::Status::FAIL) {
            logError(kComponent, "connection lost after tag " + std::to_string(m_last_tag));
            return Status::FAIL;
        }

        if (auto record = m_parser->parse(msg.body)) {
            out.push_back(std::move(*record));
            ++m_parsed;
        } else {
            ++m_rejected;
        }

        m_last_tag = msg.tag;
    }

    logDebug(kComponent, "batch of " + std::to_string(out.size()) + " records");

    return m_stop ? Status::DONE : Status::OK;
}

uint64_t RabbitSource::last_tag() const
{
    return m_last_tag;
}

void RabbitSource::stop()
{
    m_stop = true;

    logInfo(kComponent, "stopping: " + std::to_string(m_parsed) + " parsed, "
        + std::to_string(m_rejected) + " rejected");
}

uint64_t RabbitSource::parsed() const
{
    return m_parsed;
}

uint64_t RabbitSource::rejected() const
{
    return m_rejected;
}

} // namespace cdrp

