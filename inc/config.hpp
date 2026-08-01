#pragma once

#include "constants.hpp"

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
    void load(const std::string_view& path=kConfigPath);

    // Validate the loaded configuration.
    void validate();

public:
    bool conf = false;

    std::string_view mode;

    struct {
        std::string_view dir;
        int rotate_seconds;
    } file;

    struct {
        std::string_view url;
        std::string_view queue;
    } rabbit;
};

} // namespace cdrp

