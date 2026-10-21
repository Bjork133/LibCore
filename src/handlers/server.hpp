#pragma once

#include <string>
#include <vector>

namespace libcore::handlers {

// Диспетчер подкоманд `libcore server ...`.
//   server init [--name N] [--host U] [--port P]
//   server list
//   server code [NAME]
//   server status [NAME]
int cmd_server(const std::vector<std::string>& args, bool json);

} // namespace libcore::handlers
