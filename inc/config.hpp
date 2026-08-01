#pragma once

#include "constants.hpp"

#include <string_view>

namespace cdrp {

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
    bool m_conf;
};

} // namespace cdrp

