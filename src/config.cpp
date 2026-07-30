#include "config.hpp"
#include "third_party/toml.h"

namespace cdrp {

Config::Config()
{
    load();
    validate();
}

void Config::load(const std::string_view& path)
{
    auto t = toml::parse_file(path);

    m_conf = t["config"]["conf"].value_or<bool>(false);
}

void Config::validate()
{
    if (!m_conf) {
        throw std::runtime_error("Configuration is disabled");
    }
}

} // namespace cdrp

