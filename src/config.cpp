#include "config.hpp"
#include "third_party/toml.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace cdrp {

Config::Config()
{
    load();
    validate();
}

void Config::load(std::string_view path)
{
    auto t = toml::parse_file(path);

    conf = t["config"]["conf"].value_or<bool>(false);

    source.mode = t["source"]["mode"].value_or<std::string>("file");
    source.format = t["source"]["format"].value_or<std::string>("pipe");

    file.ready_dir = t["source"]["file"]["ready_dir"].value_or<std::string>("records/ready");
    file.process_dir = t["source"]["file"]["process_dir"].value_or<std::string>("records/process");
    file.done_dir = t["source"]["file"]["done_dir"].value_or<std::string>("records/done");
    file.rotate_seconds = t["generator"]["rotate_seconds"].value_or<int>(600);

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
    if (file.ready_dir.empty() || file.process_dir.empty() || file.done_dir.empty()) {
        throw std::runtime_error("File directories not set");
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

