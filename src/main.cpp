#include "config.hpp"
#include "logger.hpp"
#include "source/dir_watcher.hpp"

#include <string>
#include <vector>

using namespace cdrp;

#include "source/file_source.hpp"
#include "parser/pipe_parser.hpp"
#include "source/dir_watcher.hpp"
#include "cdr_record.hpp"
void run()
{
    DirWatcher watcher(cfg.file.ready_dir, cfg.file.process_dir);
    const PipeParser parser;
    std::string record_path;

    while (watcher.next_file(record_path)) {
        FileSource fs(record_path, parser);
        std::vector<CdrRecord> out;
        while (fs.next(out) == FileSource::Status::OK) {
            logInfo("Processor", "parsed " + std::to_string(out.size()) + " records");
        }
    }
}

int main()
{
    logInfo("Processor", "starting: '" + cfg.source.mode + "' mode, '" + cfg.source.format + "' format");

    run();
    return 0;
}

