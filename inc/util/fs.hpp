#pragma once

#include <string>

namespace cdrp {

/**
 * Create dir and any missing parents, failures are logged not thrown.
 *
 * @param dir: the directory to create
 * @return true if the directory exists after the call
 */
bool ensure_dir(const std::string& dir);

/**
 * Last path component of path.
 *
 * @param path: the path to strip
 * @return the text after the last '/', or path when it holds none
 */
std::string basename_of(const std::string& path);

} // namespace cdrp
