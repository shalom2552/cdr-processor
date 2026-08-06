#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "logger.hpp"

TEST_CASE("test_tests")
{
    CHECK(true);
}

int main(int argc, char** argv)
{
    cdrp::Logger::instance().setLevel(cdrp::LogLevel::None);
    return doctest::Context(argc, argv).run();
}
