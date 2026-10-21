#pragma once

#include <string>
#include <vector>

namespace libcore::handlers {

int cmd_stats(const std::vector<std::string>& args, bool json_out);

} // namespace libcore::handlers
