#pragma once

#include "parser/iparser.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cdrp {

/*
 * Maps a format name to a parser. Holds the known parsers and builds one on
 * demand. Single shared instance; register at startup, create per file.
 */
class ParserFactory {
public:
    using Creator = std::function<std::unique_ptr<IParser>()>;

    static ParserFactory& instance();
    ~ParserFactory() = default;

    ParserFactory(const ParserFactory&) = delete;
    ParserFactory& operator=(const ParserFactory&) = delete;

    /* Register a parser under name, replacing any parser already under it. */
    void registerParser(const std::string& name, Creator creator);

    /* True if a parser is registered under name. */
    bool hasParser(const std::string& name) const;

    /* Build a new parser for name, or nullptr if none is registered. */
    std::unique_ptr<IParser> createParser(const std::string& name) const;

private:
    ParserFactory();

private:
    std::unordered_map<std::string, Creator> m_parsers;
};

} // namespace cdrp

