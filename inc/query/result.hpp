#pragma once

#include <string>

namespace cdrp {

/* A response: the status to send and the JSON body to send with it */
struct Result {
    int status = 200;
    std::string body;
};

} // namespace cdrp
