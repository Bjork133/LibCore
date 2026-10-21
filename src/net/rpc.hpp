#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace libcore::net {

using json = nlohmann::json;

// Обработать один RPC-запрос.
//   library — имя библиотеки (для открытия БД)
//   request — JSON вида { "cmd": "...", "args": {...}, "token": "..." }
//
// Возвращает JSON-ответ: { "ok": true, "data": ... } или
//                         { "ok": false, "error": { "code": "...", "message": "..." } }
json dispatch_rpc(const std::string& library, const json& request);

// Вспомогательные ответы.
json rpc_ok(json data = json::object());
json rpc_error(const std::string& code, const std::string& message);

} // namespace libcore::net
