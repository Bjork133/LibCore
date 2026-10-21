#pragma once

#include <string>
#include <vector>

namespace libcore::handlers {

int cmd_login (const std::vector<std::string>& args, bool json_out);
int cmd_logout(const std::vector<std::string>& args, bool json_out);
int cmd_whoami(const std::vector<std::string>& args, bool json_out);

} // namespace libcore::handlers
