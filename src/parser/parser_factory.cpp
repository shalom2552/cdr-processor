#include "parser/parser_factory.hpp"

#include "parser/csv_parser.hpp"
#include "config.hpp"
#include "logger.hpp"

#include <utility>

constexpr std::string_view kComponent = "ParserFactory";

namespace cdrp {

ParserFactory::ParserFactory()
{
    registerParser("csv", []() { return std::make_unique<CsvParser>(cfg.csv.separator); });
}

ParserFactory& ParserFactory::instance()
{
    static ParserFactory instance;
    return instance;
}

void ParserFactory::registerParser(const std::string& name, Creator creator)
{
    m_parsers[name] = std::move(creator);
}

bool ParserFactory::hasParser(const std::string& name) const
{
    return m_parsers.count(name) != 0;
}

std::unique_ptr<IParser> ParserFactory::createParser(const std::string& name) const
{
    auto it = m_parsers.find(name);
    if (it == m_parsers.end()) {
        logDebug(kComponent, "no parser for name: " + name);
        return nullptr;
    } else {
        return it->second();
    }
}


} // namespace cdrp

