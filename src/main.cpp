#include "config.hpp"
#include "logger.hpp"

#include <string>
#include <vector>

using namespace cdrp;

#include "source/file_source.hpp"
#include "parser/pipe_parser.hpp"
#include "cdr_record.hpp"
void run()
{
    const PipeParser parser;
    FileSource fs("records/20260804_005720.cdr", parser);
    std::vector<CdrRecord> out;
    fs.next(out);
    logInfo("Parsed records: " + std::to_string(out.size()));
}

int main()
{
    logInfo("Starting Processor application");
    logInfo("Config value: " + std::string(cfg.conf ? "true" : "false"));

    run();
    return 0;
}

