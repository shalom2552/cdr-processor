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
    mode = t["source"]["mode"].value_or<std::string>("file");

    file.dir = t["file"]["dir"].value_or<std::string>("records");
    file.rotate_seconds = t["file"]["rotate_seconds"].value_or<int>(600);

    rabbit.url = t["rabbit"]["url"].value_or<std::string>("amqp://guest:guest@localhost/");
    rabbit.queue = t["rabbit"]["queue"].value_or<std::string>("cdr");

    log.level = t["log"]["level"].value_or<std::string>("info");
}

void Config::validate()
{
    if (!conf) {
        throw std::runtime_error("Configuration not loaded");
    }
    if (mode != "file" && mode != "rabbit") {
        throw std::runtime_error("Invalid mode: " + mode + "\nValid modes are file and rabbit.");
    }
    if (file.dir.empty()) {
        throw std::runtime_error("File directory not set");
    }
    if (file.rotate_seconds <= 0) {
        throw std::runtime_error("Rotate seconds must be greater than zero");
    }
    if (rabbit.url.empty() || rabbit.queue.empty()) {
        throw std::runtime_error("RabbitMQ URL or queue not set");
    }
    if (log.level != "info" && log.level != "debug" && log.level != "warn" && log.level != "error") {
        throw std::runtime_error("Invalid log level: " + log.level + "\nValid levels are info, debug, warn, error.");
    }
}

} // namespace cdrp

