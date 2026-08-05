#include "config.hpp"
#include "logger.hpp"
#include "third_party/toml.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace cdrp {

Config::Config()
{
    load();
    validate();

    Logger::instance().setLevel(Logger::levelFromName(log.level));
    logDebug("Config", "loaded " + std::string(kConfigPath) + ": " + source.mode + " mode, "
        + source.format + " format, " + log.level + " level, ready " + file.ready_dir);
}

void Config::load(std::string_view path)
{
    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        logError("Config", "cannot parse " + std::string(path) + ": " + std::string(e.description()));
        throw std::runtime_error("Configuration not loaded");
    }

    conf = t["config"]["conf"].value_or<bool>(false);

    source.mode = t["source"]["mode"].value_or<std::string>("file");
    source.format = t["source"]["format"].value_or<std::string>("pipe");

    file.rotate_seconds = t["generator"]["rotate_seconds"].value_or<int>(600);
    file.readers = t["source"]["file"]["readers"].value_or<std::size_t>(4);
    file.ready_dir = t["source"]["file"]["ready_dir"].value_or<std::string>("records/ready");
    file.process_dir = t["source"]["file"]["process_dir"].value_or<std::string>("records/process");
    file.done_dir = t["source"]["file"]["done_dir"].value_or<std::string>("records/done");
    file.fail_dir = t["source"]["file"]["fail_dir"].value_or<std::string>("records/failed");

    rabbit.url = t["source"]["rabbit"]["url"].value_or<std::string>("amqp://guest:guest@localhost/");
    rabbit.queue = t["source"]["rabbit"]["queue"].value_or<std::string>("cdr");

    log.level = t["log"]["level"].value_or<std::string>("info");
}

void Config::validate()
{
    if (!conf) {
        throw std::runtime_error("Configuration not loaded");
    }
    if (source.mode != "file" && source.mode != "rabbit") {
        throw std::runtime_error("Invalid mode: " + source.mode + "\nValid modes are file and rabbit.");
    }
    if (source.format != "pipe" && source.format != "json") {
        std::string formats = "pipe";
        throw std::runtime_error("Invalid format: " + source.format + "\nValid formats are" + formats + ".");
    }
    if (file.ready_dir.empty() || file.process_dir.empty() || file.done_dir.empty() || file.fail_dir.empty() ) {
        throw std::runtime_error("File directories not set");
    } else {
        for (std::string* dir : {&file.ready_dir, &file.process_dir, &file.done_dir, &file.fail_dir}) {
            if (dir->back() != '/') {
                dir->push_back('/');
            }
        }
    }
    if (file.readers == 0) {
        unsigned n = std::thread::hardware_concurrency();
        file.readers = (n == 0) ? 4 : n;
        logInfo("Config", "setting max file readers: " + std::to_string(file.readers));
    }
    if (file.rotate_seconds <= 0) {
        throw std::runtime_error("Rotate seconds must be greater than zero");
    }

    if (rabbit.url.empty() || rabbit.queue.empty()) {
        throw std::runtime_error("RabbitMQ URL or queue not set");
    }

    if (log.level != "info" && log.level != "debug" && log.level != "warn" && log.level != "error") {
        std::string levels = "info, debug, warn, error";
        throw std::runtime_error("Invalid log level: " + log.level + "\nValid levels are" + levels + ".");
    }
}

} // namespace cdrp

