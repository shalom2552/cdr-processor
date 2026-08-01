#include "config.hpp"
#include <iostream>

using namespace cdrp;
int main()
{
    const Config& cfg = Config::instance();

    std::cout << "Hello, World!" << std::endl;
    std::cout << "Config value: " << cfg.conf << std::endl;
    return 0;
}

