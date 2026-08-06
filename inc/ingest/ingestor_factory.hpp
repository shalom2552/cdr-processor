#pragma once

#include "ingest/iingestor.hpp"
#include "sink/isink.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cdrp {

/*
 * Maps a source mode to an ingestor. Holds the known ingestors and builds one on
 * demand. Single shared instance; register at startup, create once per run.
 */
class IngestorFactory {
public:
    using Creator = std::function<std::unique_ptr<IIngestor>(ISink&)>;

    static IngestorFactory& instance();
    ~IngestorFactory() = default;

    IngestorFactory(const IngestorFactory&) = delete;
    IngestorFactory& operator=(const IngestorFactory&) = delete;

    /* Register an ingestor under name, replacing any ingestor already under it. */
    void registerIngestor(const std::string& name, Creator creator);

    /* True if an ingestor is registered under name. */
    bool hasIngestor(const std::string& name) const;

    /* Build a new ingestor for name feeding sink, or nullptr if none is registered. */
    std::unique_ptr<IIngestor> createIngestor(const std::string& name, ISink& sink) const;

private:
    IngestorFactory();

private:
    std::unordered_map<std::string, Creator> m_ingestors;
};

} // namespace cdrp

