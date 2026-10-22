#pragma once

#include "api/common.hpp"

#include <string>

namespace libcore::api {

// Login по username/password. Возвращает {ok, data: {username, role, token}} или {ok:false}.
json login(const std::string& library,
           const std::string& username,
           const std::string& password);

// Регистрация по коду библиотеки. Создаёт reader.
json register_user(const std::string& library,
                   const std::string& code,
                   const std::string& username,
                   const std::string& password);

// Whoami по токену.
json whoami(const std::string& library, const std::string& token);

// Logout — удаляет сессию.
json logout(const std::string& library, const std::string& token);

} // namespace libcore::api
