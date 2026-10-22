#pragma once

#include "api/common.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// Показать флаги приватности. Без target — свои.
json profile_show(const std::string& library, const std::string& token,
                  const std::optional<std::string>& target);

// Установить флаг приватности.
json profile_set(const std::string& library, const std::string& token,
                 const std::string& field,
                 bool value);

} // namespace libcore::api
