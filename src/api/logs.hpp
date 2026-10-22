#pragma once

#include "api/common.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// Логи с фильтрами.
json logs(const std::string& library, const std::string& token,
          const std::optional<std::string>& level,
          const std::optional<std::string>& event,
          const std::optional<std::string>& since,
          int tail);

} // namespace libcore::api
