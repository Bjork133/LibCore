// version.cpp
#include "handlers/version.hpp"

#include <cstdio>

#ifndef LIBCORE_VERSION
#  define LIBCORE_VERSION "0.0.0"
#endif

namespace libcore::handlers {

int cmd_version(bool json) {
    if (json) {
        std::printf("{\"ok\":true,\"data\":{\"version\":\"%s\"}}\n", LIBCORE_VERSION);
    } else {
        std::printf("libcore %s\n", LIBCORE_VERSION);
    }
    return 0;
}

} // namespace libcore::handlers
