#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "config.hpp"

TEST_CASE("test_tests")
{
    CHECK(true);
}

TEST_CASE("test_config")
{
    const cdrp::Config& config = cdrp::Config::instance();
    CHECK(config.m_conf);
}
