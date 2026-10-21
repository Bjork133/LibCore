#pragma once

#include "core/profiles.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace libcore::handlers {

using json = nlohmann::json;

// Универсальный вызов RPC для remote-профиля.
// Если response.ok == false — печатает ошибку и возвращает код 1.
// Если ok — печатает data (pretty human или json) и возвращает 0.
int remote_call(const core::Profile& profile,
                const std::string& cmd,
                const json& args,
                bool json_out);

// Просто отправить и получить ответ, без печати.
// Бросает std::runtime_error при сетевой ошибке.
json remote_raw(const core::Profile& profile,
                const std::string& cmd,
                const json& args = json::object());

} // namespace libcore::handlers
