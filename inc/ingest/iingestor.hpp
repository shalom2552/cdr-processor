#pragma once

namespace cdrp {

class IIngestor {
public:
    virtual ~IIngestor() = default;

    IIngestor(const IIngestor&)            = delete;
    IIngestor& operator=(const IIngestor&) = delete;

    /* Begin ingesting. Returns false if the ingestor could not start. */
    virtual bool start() = 0;

    /* Stop ingesting and release resources. */
    virtual void stop() = 0;

protected:
    IIngestor() = default;

};

} // namespace cdrp

