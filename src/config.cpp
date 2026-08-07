#include "config.hpp"

#include "logger.hpp"
#include "toml.h"

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
    logDebug("Config", "loaded " + std::string(kConfigPath));
    logDebug("Config", "source: " + source.mode + " mode, " + source.format + " format");
    logDebug("Config", "csv: " + std::string(1, csv.separator) + " separator");
    logDebug("Config", "file: " + std::to_string(file.readers) + " readers, ready " + file.ready_dir
        + ", process " + file.process_dir);
    logDebug("Config", "file: done " + file.done_dir + ", failed " + file.fail_dir
        + ", rotate " + std::to_string(file.rotate_seconds) + "s");
    logDebug("Config", "rabbit: " + std::to_string(rabbit.consumers) + " consumers, queue "
        + rabbit.queue + ", url " + rabbit.url);
    logDebug("Config", "redis: " + redis.host + ":" + std::to_string(redis.port)
        + ", timeout " + std::to_string(redis.timeout_ms) + "ms");
    logDebug("Config", "log: " + log.level + " level");
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

    source.mode = t["source"]["mode"].value_or<std::string>("file");
    source.format = t["source"]["format"].value_or<std::string>("csv");

    csv.separator = t["source"]["csv"]["separator"].value_or<std::string>("|")[0];

    file.rotate_seconds = t["generator"]["rotate_seconds"].value_or<int>(600);
    file.readers = t["source"]["file"]["readers"].value_or<std::size_t>(4);
    file.ready_dir = t["source"]["file"]["ready_dir"].value_or<std::string>("records/ready");
    file.process_dir = t["source"]["file"]["process_dir"].value_or<std::string>("records/process");
    file.done_dir = t["source"]["file"]["done_dir"].value_or<std::string>("records/done");
    file.fail_dir = t["source"]["file"]["fail_dir"].value_or<std::string>("records/failed");

    rabbit.consumers = t["source"]["rabbit"]["consumers"].value_or<std::size_t>(4);
    rabbit.url = t["source"]["rabbit"]["url"].value_or<std::string>("amqp://guest:guest@localhost/");
    rabbit.queue = t["source"]["rabbit"]["queue"].value_or<std::string>("cdr");

    redis.host = t["redis"]["host"].value_or<std::string>("127.0.0.1");
    redis.port = t["redis"]["port"].value_or<int>(6379);
    redis.timeout_ms = t["redis"]["timeout_ms"].value_or<int>(1000);

    log.level = t["log"]["level"].value_or<std::string>("info");
}

void Config::validate()
{
    if (source.mode != "file" && source.mode != "rabbit") {
        throw std::runtime_error("Invalid mode: " + source.mode + "\nValid modes are file and rabbit.");
    }
    if (source.format != "csv") {
        std::string formats = "csv";
        throw std::runtime_error("Invalid format: " + source.format + "\nValid formats are" + formats + ".");
    }
    if (source.format == "csv" && csv.separator == '\0') {
        throw std::runtime_error("Separator not set");
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
    if (rabbit.consumers == 0) {
        unsigned n = std::thread::hardware_concurrency();
        rabbit.consumers = (n == 0) ? 4 : n;
        logInfo("Config", "setting max rabbit consumers: " + std::to_string(rabbit.consumers));
    }

    if (redis.host.empty()) {
        throw std::runtime_error("Redis host not set");
    }
    if (redis.port <= 0 || redis.port > UINT16_MAX) {
        throw std::runtime_error("Invalid redis port: " + std::to_string(redis.port));
    }
    if (redis.timeout_ms <= 0) {
        throw std::runtime_error("Redis timeout must be greater than zero");
    }

    if (log.level != "info" && log.level != "debug" && log.level != "warn" && log.level != "error") {
        std::string levels = "info, debug, warn, error";
        throw std::runtime_error("Invalid log level: " + log.level + "\nValid levels are" + levels + ".");
    }
}

} // namespace cdrp

