// help.hpp
#pragma once

#include <string>
#include <vector>

namespace libcore::handlers {
int cmd_help(const std::vector<std::string>& args, bool json);
}
