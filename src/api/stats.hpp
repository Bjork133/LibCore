#pragma once

#include "api/common.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// Статистика юзера. Без target — своя.
json stats(const std::string& library, const std::string& token,
           const std::optional<std::string>& target);

} // namespace libcore::api
