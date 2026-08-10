#pragma once

#include "constants.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace cdrp {

/**
 * @class Config
 * A singletone class that holds application configuration.
 */
class Config {
public:
    static const Config& instance() {
        static Config config;
        return config;
    }

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    Config();

    // Load configuration from a file.
    void load(std::string_view path=kConfigPath);

    // Validate the loaded configuration.
    void validate();

public:
    struct {
        std::string mode;
        std::string format;
    } source;

    struct {
        char separator;
    } csv;

    struct {
        std::string level;
    } log;

    struct {
        std::size_t readers;
        std::string ready_dir;
        std::string process_dir;
        std::string done_dir;
        std::string fail_dir;
        int rotate_seconds = 0;
    } file;

    struct {
        std::size_t consumers;
        std::string url;
        std::string queue;
    } rabbit;

    struct {
        std::string type;
    } store;

    struct {
        std::string host;
        int port;
        int timeout_ms;
    } redis;

    struct {
        int port;
        std::string host;
        std::size_t concurrency;
        std::size_t max_hops;
        std::size_t max_visited;
    } query;
};

inline const Config& cfg = Config::instance();

} // namespace cdrp

