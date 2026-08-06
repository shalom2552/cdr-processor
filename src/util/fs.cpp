#include "util/fs.hpp"
#include "logger.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace cdrp {

bool ensure_dir(const std::string& dir)
{
    std::error_code ec;
    if (fs::create_directories(dir, ec)) {
        logInfo("fs", "created: " + dir);
    } else if (ec || !fs::is_directory(dir, ec)) {
        logError("fs", "cannot create: " + dir + ": " + ec.message());
        return false;
    }
    return true;
}

std::string basename_of(const std::string& path)
{
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

} // namespace cdrp
