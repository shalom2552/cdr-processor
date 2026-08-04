#include "doctest.h"
#include "config.hpp"

TEST_CASE("config_loads")
{
    CHECK(cdrp::Config::instance().conf);
}

TEST_CASE("config_mode_is_valid")
{
    const std::string& mode = cdrp::Config::instance().source.mode;
    CHECK((mode == "file" || mode == "rabbit"));
}

TEST_CASE("config_file_section")
{
    const cdrp::Config& config = cdrp::Config::instance();

    CHECK_FALSE(config.file.ready_dir.empty());
    CHECK_FALSE(config.file.process_dir.empty());
    CHECK_FALSE(config.file.done_dir.empty());
    CHECK(config.file.rotate_seconds > 0);
}

TEST_CASE("config_rabbit_section")
{
    const cdrp::Config& config = cdrp::Config::instance();

    CHECK_FALSE(config.rabbit.url.empty());
    CHECK_FALSE(config.rabbit.queue.empty());
}
