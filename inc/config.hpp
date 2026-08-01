#pragma once

#include "constants.hpp"

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
    bool conf = false;

    std::string mode;

    struct {
        std::string dir;
        int rotate_seconds = 0;
    } file;

    struct {
        std::string url;
        std::string queue;
    } rabbit;
};

} // namespace cdrp

