#pragma once

#include "api/common.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// Список юзеров. Reader видит только profile_public + себя.
json user_list(const std::string& library, const std::string& token,
               const std::optional<std::string>& role_filter);

// Детали юзера. С учётом приватности.
json user_details(const std::string& library, const std::string& token,
                  const std::string& target);

// Создать юзера (host only).
json user_add(const std::string& library, const std::string& token,
              const std::string& username,
              const std::string& password,
              const std::string& role);

// Сменить роль (host only, не себе, не host).
json user_level(const std::string& library, const std::string& token,
                const std::string& target,
                const std::string& new_role);

} // namespace libcore::api
