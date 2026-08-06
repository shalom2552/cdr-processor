#include "ingest/ingestor_factory.hpp"

#include "ingest/file_ingestor.hpp"
#include "ingest/rabbit_ingestor.hpp"
#include "logger.hpp"

#include <utility>

namespace cdrp {

IngestorFactory::IngestorFactory()
{
    registerIngestor("file", [](ISink& sink) { return std::make_unique<FileIngestor>(sink); });
    registerIngestor("rabbit", [](ISink& sink) { return std::make_unique<RabbitIngestor>(sink); });
}

IngestorFactory& IngestorFactory::instance()
{
    static IngestorFactory instance;
    return instance;
}

void IngestorFactory::registerIngestor(const std::string& name, Creator creator)
{
    m_ingestors[name] = std::move(creator);
}

bool IngestorFactory::hasIngestor(const std::string& name) const
{
    return m_ingestors.count(name) != 0;
}

std::unique_ptr<IIngestor> IngestorFactory::createIngestor(const std::string& name, ISink& sink) const
{
    auto it = m_ingestors.find(name);
    if (it == m_ingestors.end()) {
        logDebug("IngestorFactory", "no ingestor for mode: " + name);
        return nullptr;
    } else {
        return it->second(sink);
    }
}

} // namespace cdrp

