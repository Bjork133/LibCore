#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace libcore::api {

using json = nlohmann::json;

// Успешный ответ: {ok: true, data: {...}}
inline json ok(json data = json::object()) {
    json j;
    j["ok"] = true;
    j["data"] = std::move(data);
    return j;
}

// Ошибка: {ok: false, error: {code, message}}
inline json err(const std::string& code, const std::string& message) {
    json j;
    j["ok"] = false;
    j["error"]["code"] = code;
    j["error"]["message"] = message;
    return j;
}

// Контекст сессии — то, что нужно для авторизации.
struct Context {
    long long user_id = 0;
    std::string username;
    std::string role;
};

} // namespace libcore::api
