#pragma once

#include "api/common.hpp"
#include "db/database.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// Открыть БД библиотеки + прогнать миграции.
db::Database open_library(const std::string& library);

// Получить контекст по токену. nullopt — неверный/просроченный токен.
std::optional<Context> auth_by_token(db::Database& database, const std::string& token);

// Проверка ролей.
bool role_at_least(const std::string& role, const std::string& required);

} // namespace libcore::api
