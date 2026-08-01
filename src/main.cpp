#include "config.hpp"
#include "logger.hpp"

#include <string>

using namespace cdrp;
int main()
{
    logInfo("Config value: " + std::string(cfg.conf ? "true" : "false"));
    return 0;
}

