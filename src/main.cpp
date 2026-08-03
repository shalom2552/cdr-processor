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
    logInfo("[Processor] parsed " + std::to_string(out.size()) + " records");
}

int main()
{
    logInfo("[Processor] starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format
        + "' format, records in /" + cfg.file.dir);

    run();
    return 0;
}

